#include "program_instance.hpp"
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>

namespace kvm {

static constexpr int MAX_EVENTS = 8;
static constexpr size_t MAX_READ_BUFFER = 128UL * 1024;
static constexpr size_t MAX_VM_WR_BUFFERS = 64;
static constexpr float CALLBACK_TIMEOUT = 8.0f; /* Seconds */

LongLived::LongLived(std::shared_ptr<ProgramInstance> prog)
	: m_prog_ref(std::move(prog))
{
	this->m_epoll_fd = epoll_create(MAX_EVENTS);
	this->m_epoll_thread = std::thread(&LongLived::epoll_main_loop, this);
}
LongLived::~LongLived()
{
	this->m_running = false;
	/* TODO: Interrupt epoll thread. */
	m_epoll_thread.join();

	close(this->m_epoll_fd);
}

LongLived& ProgramInstance::epoll_system(std::shared_ptr<ProgramInstance> prog)
{
	std::scoped_lock lck(long_lived_mtx);

	if (this->m_epoll_system == nullptr) {
		this->m_epoll_system.reset(new LongLived(std::move(prog)));
	}
	return *this->m_epoll_system;
}

void LongLived::epoll_main_loop()
{
	struct epoll_event events[MAX_EVENTS];

	while (this->m_running)
	{
		const int nfds =
			epoll_wait(this->m_epoll_fd, &events[0], MAX_EVENTS, -1);
		for (int i = 0; i < nfds; i++)
		{
			auto& ev = events[i];
			if (ev.events & (EPOLLIN))
			{
				try {
					this->data(ev.data.fd);
				} catch (...) {
					/* XXX: Implement me. */
				}
			}
			if (ev.events & (EPOLLHUP))
			{
				try {
					this->hangup(ev.data.fd);
				} catch (...) {
					/* XXX: Implement me. */
				}
				epoll_ctl(this->m_epoll_fd, EPOLL_CTL_DEL, ev.data.fd, NULL);
				close(ev.data.fd);
			}
		}
	}
}

bool LongLived::manage(int fd)
{
	/* Make non-blocking */
	int r = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (r < 0)
		return false;

	/* Enable TCP keepalive */
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));

	/* Add to epoll */
	struct epoll_event event;
	event.events = EPOLLIN; /* TODO: Edge-triggered? */
	event.data.fd = fd;
	if (epoll_ctl(this->m_epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
	{
		return false;
	}

	/* Task queue with one reader thread. */
	auto fut = program().m_storage_queue.enqueue(
		[this, fd] () -> long
		{
			auto& storage = *this->program().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKET_CONNECTED);
			if (func == 0x0) {
				/* Silently skip on_connected callback. */
				return true;
			}

			const int virtual_fd = 0x1000 + fd;
			storage.file_descriptors().manage(fd, virtual_fd);

			/* Call the storage VM on_connected callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				virtual_fd);

			/* Get answer from VM, and unmanage the fd if no. */
			const bool answer = storage.machine().return_value();
			if (!answer) {
				storage.file_descriptors().free_byhash(virtual_fd);
			}

			return answer;
		});
	/* The virtual machine has final say, regarding managing the fd. */
	return fut.get();
}
void LongLived::data(int fd)
{
	auto fut = program().m_storage_queue.enqueue(
		[this, fd] () -> long
		{
			auto& storage = *this->program().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKET_DATA);
			if (func == 0x0) {
				/* Silently skip on_data callback. */
				return true;
			}

			if (this->m_read_vaddr == 0x0) {
				this->m_read_vaddr = storage.machine().mmap_allocate(MAX_READ_BUFFER);
			}
			/* Gather buffers from writable area */
			tinykvm::Machine::WrBuffer buffers[MAX_VM_WR_BUFFERS];
			const auto n_buffers =
				storage.machine().writable_buffers_from_range(MAX_VM_WR_BUFFERS,
					buffers, this->m_read_vaddr, MAX_READ_BUFFER);

			while (true)
			{
				ssize_t len =
					readv(fd, (struct iovec *)&buffers[0], n_buffers);
				if (len <= 0)
					break;

				/* Call the storage VM on_data callback. */
				const int virtual_fd = 0x1000 + fd;
				storage.machine().timed_vmcall(
					func, CALLBACK_TIMEOUT,
					virtual_fd,
					m_read_vaddr, len);
			}
			return 0;
		});
	fut.get();
}
void LongLived::hangup(int fd)
{
	auto fut = program().m_storage_queue.enqueue(
		[this, fd] () -> long
		{
			auto& storage = *this->program().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKED_DISCONNECTED);
			if (func == 0x0) {
				/* Silently skip on_disconnected callback. */
				return 0;
			}

			const int virtual_fd = 0x1000 + fd;

			/* Call the storage VM on_disconnected callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				virtual_fd);

			return 0;
		});
	fut.get();
}

} // kvm
