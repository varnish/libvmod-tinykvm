#include "program_instance.hpp"
#include <arpa/inet.h>
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
			if (ev.events & EPOLLIN)
			{
				try {
					const auto len = this->fd_readable(ev.data.fd);
					if (len <= 0) {
						/* Some errors are not fatal */
						const bool non_fatal = (len == -1 && errno == EWOULDBLOCK);
						if (!non_fatal) {
							// Close immediately (???)
							this->hangup(ev.data.fd, (len == 0) ? "Disconnected" : "Error");

							// Skip over the remaining events for this fd
							continue;
						}
					}
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll read exception: %s\n", e.what());
				}
			}
			if (ev.events & EPOLLOUT)
			{
				try {
					this->fd_writable(ev.data.fd);
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll write exception: %s\n", e.what());
				}
			}
			if (ev.events & EPOLLHUP)
			{
				try {
					this->hangup(ev.data.fd, "Hangup");
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll hangup exception: %s\n", e.what());
				}
			}
		}
	}
}

bool LongLived::epoll_add(const int fd)
{
	/* Add to epoll, edge-triggered. */
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLET;
	event.data.fd = fd;
	if (epoll_ctl(this->m_epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
	{
		/* XXX: Silently close when it can't be added. */
		close(fd);
		return false;
	}
	return true;
}

bool LongLived::manage(const int fd, const char *argument)
{
	/* Make non-blocking */
	int r = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (r < 0)
		return false;

	/* Enable TCP keepalive */
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));

	/* Don't call on_connect if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKET_CONNECTED) == 0x0)
	{
		return this->epoll_add(fd);
	}

	/* Task queue with one reader thread. */
	auto fut = program().m_storage_queue.enqueue(
		[this, fd, argument] () -> long
		{
			auto& storage = *this->program().storage().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKET_CONNECTED);

			const int virtual_fd = 0x1000 + fd;
			storage.file_descriptors().manage(fd, virtual_fd);

			/* Translate peer sockaddr to string */
			struct sockaddr_in sin;
			socklen_t silen = sizeof(sin);
			getsockname(fd, (struct sockaddr *)&sin, &silen);

			char ip_buffer[INET_ADDRSTRLEN];
			const char *peer =
				inet_ntop(AF_INET, &sin.sin_addr, ip_buffer, sizeof(ip_buffer));
			if (peer == nullptr)
				peer = "(unknown)";

			/* Call the storage VM on_connected callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				int(virtual_fd), peer, argument);

			/* Get answer from VM, and unmanage the fd if no. */
			const bool answer = storage.machine().return_value();
			if (!answer) {
				storage.file_descriptors().free_byhash(virtual_fd);
			}

			return answer;
		});
	/* The virtual machine has final say, regarding managing the fd. */
	const long ret = fut.get();
	if (ret) {
		return this->epoll_add(fd);
	}
	return false;
}
long LongLived::fd_readable(int fd)
{
	/* Don't call on_data if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKET_DATA) == 0x0) {
		/* Let's not read anymore. */
		shutdown(fd, SHUT_RD);
		return 0;
	}

	auto fut = program().m_storage_queue.enqueue(
		[this, fd] () -> long
		{
			auto& storage = *this->program().storage().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKET_DATA);

			if (this->m_read_vaddr == 0x0) {
				this->m_read_vaddr = storage.machine().mmap_allocate(MAX_READ_BUFFER);
			}
			/* Gather buffers from writable area */
			tinykvm::Machine::WrBuffer buffers[MAX_VM_WR_BUFFERS];
			const auto n_buffers =
				storage.machine().writable_buffers_from_range(MAX_VM_WR_BUFFERS,
					buffers, this->m_read_vaddr, MAX_READ_BUFFER);

			ssize_t len = MAX_READ_BUFFER;
			while (len == MAX_READ_BUFFER)
			{
				len = readv(fd, (struct iovec *)&buffers[0], n_buffers);
				/* XXX: Possibly very stupid reason to break. But we can always come back. */
				if (len < 0)
					break;

				/* Call the storage VM on_data callback. */
				const int virtual_fd = 0x1000 + fd;
				storage.machine().timed_vmcall(
					func, CALLBACK_TIMEOUT,
					int(virtual_fd),
					m_read_vaddr, ssize_t(len));
			}
			return len;
		});
	return fut.get();
}
void LongLived::fd_writable(int fd)
{
	/* Don't call on_data if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKET_WRITABLE) == 0x0)
		return;

	auto fut = program().m_storage_queue.enqueue(
		[this, fd] () -> long
		{
			auto& storage = *this->program().storage().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKET_WRITABLE);

			const int virtual_fd = 0x1000 + fd;

			/* Call the storage VM on_writable callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				int(virtual_fd));
			return 0;
		});
	fut.get();
}

void LongLived::hangup(int fd, const char *reason)
{
	/* Preemptively close and remove the fd. */
	epoll_ctl(this->m_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
	program().storage().storage_vm->file_descriptors().free_byval(fd);

	/* Don't call on_disconnect if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKED_DISCONNECTED) == 0x0)
		return;

	auto fut = program().m_storage_queue.enqueue(
		[this, fd, reason] () -> long
		{
			auto& storage = *this->program().storage().storage_vm;
			auto func = program().
				entry_at(ProgramEntryIndex::SOCKED_DISCONNECTED);

			const int virtual_fd = 0x1000 + fd;

			/* Call the storage VM on_disconnected callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				int(virtual_fd), (const char *)reason);

			return 0;
		});
	fut.get();
}

} // kvm
