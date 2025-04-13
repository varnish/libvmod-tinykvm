#include "../program_instance.hpp"
#include "../tenant_instance.hpp"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace kvm {

static constexpr int MAX_EVENTS = 8;
static constexpr size_t MAX_READ_BUFFER = 1UL << 20; /* 1MB */
static constexpr size_t MAX_VM_WR_BUFFERS = 64;
static constexpr float CALLBACK_TIMEOUT = 8.0f; /* Seconds */

EpollServer::EpollServer(const TenantInstance* tenant, ProgramInstance* prog, int16_t id)
	: m_system_id(id),
	  m_tenant(tenant),
	  m_program(prog)
{
	if (!prog->has_storage()) {
		throw std::runtime_error("EpollServer requires a storage VM");
	}

	this->m_epoll_fd = epoll_create(MAX_EVENTS);
	const std::string& address = tenant->config.group.server_address;
	if (address.empty()) {
		throw std::runtime_error("Invalid server address: '" + address + "'");
	}

	// Create a listening UNIX socket
	this->m_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (this->m_listen_fd < 0) {
		throw std::runtime_error("Failed to create socket");
	}
	// Set the socket to reuse the address
	int opt = 1;
	if (setsockopt(this->m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to set socket options");
	}
	// Set up the server address (default to "/tmp/kvm.sock")
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "/tmp/kvm.sock");
	unlink(addr.sun_path); // Remove any existing socket
	// Bind the socket to the address
	if (bind(this->m_listen_fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to bind socket");
	}
	// Listen for incoming connections
	if (listen(this->m_listen_fd, SOMAXCONN) < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to listen on socket");
	}
	// Add the listening socket to epoll
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = this->m_listen_fd;
	if (epoll_ctl(this->m_epoll_fd, EPOLL_CTL_ADD, this->m_listen_fd, &event) < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to add socket to epoll");
	}
	// Create an eventfd for signaling, so we can stop the epoll thread
	this->m_event_fd = eventfd(0, EFD_NONBLOCK);
	if (this->m_event_fd < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to create eventfd");
	}
	// Add the eventfd to epoll
	event.events = EPOLLIN;
	event.data.fd = this->m_event_fd;
	if (epoll_ctl(this->m_epoll_fd, EPOLL_CTL_ADD, this->m_event_fd, &event) < 0) {
		close(this->m_listen_fd);
		close(this->m_event_fd);
		throw std::runtime_error("Failed to add eventfd to epoll");
	}
	// Set the server to running state
	this->m_running = true;
	printf("epoll server started on path '%s'\n", address.c_str());
	// Start the epoll thread
	this->m_epoll_thread = std::thread(&EpollServer::epoll_main_loop, this);
}
void EpollServer::stop()
{
	this->m_running = false;
	/* Notify the epoll thread to stop */
	uint64_t u = 1;
	if (eventfd_write(this->m_event_fd, u) < 0) {
		fprintf(stderr, "Failed to write to eventfd: %s\n", strerror(errno));
	}
	/* TODO: Interrupt epoll thread. */
	m_epoll_thread.join();

	close(this->m_epoll_fd);
	close(this->m_listen_fd);
	close(this->m_event_fd);
}
EpollServer::~EpollServer()
{
	if (this->m_running) {
		this->stop();
	}
}

void EpollServer::epoll_main_loop()
{
	struct epoll_event events[MAX_EVENTS];
	std::vector<int> new_fds;
	std::future<long> new_fds_future;
	// A list of futures that we delay until the end of the epoll loop
	futures_t futures;

	while (this->m_running)
	{
		const int nfds =
			epoll_wait(this->m_epoll_fd, &events[0], MAX_EVENTS, -1);
		for (int i = 0; i < nfds; i++)
		{
			auto& ev = events[i];
			if (ev.events & EPOLLIN)
			{
				if (ev.data.fd == this->m_listen_fd)
				{
					while (true) {
						/* Accept new connection */
						int fd = accept(this->m_listen_fd, nullptr, nullptr);
						if (fd < 0) {
							//fprintf(stderr, "epoll accept error: %s\n", strerror(errno));
							break;
						}
						if (!this->manage(fd, new_fds)) {
							fprintf(stderr, "epoll manage error: %s\n", strerror(errno));
							close(fd);
						}
					}
					continue; /* Move to next event */
				} else if (ev.data.fd == this->m_event_fd)
				{
					/* Eventfd triggered, stop the server */
					uint64_t u;
					eventfd_read(this->m_event_fd, &u);
					this->m_running = false;
					continue; /* Move to next event */
				}
				try {
					this->fd_readable(ev.data.fd, futures);
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll read exception: %s\n", e.what());
				}
			}
			if (ev.events & EPOLLOUT)
			{
				try {
					this->fd_writable(ev.data.fd, futures);
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
		} // epoll_wait
		/* Handle all new connections in the queue */
		if (!new_fds.empty()) {
			if (new_fds_future.valid()) {
				/* Wait for the previous task to finish */
				new_fds_future.get();
			}
			new_fds_future = this->handle_new_connections(new_fds, nullptr);
			new_fds.clear();
		}
		/* Wait for all futures to finish */
		for (auto& f : futures) {
			if (f.valid()) {
				FdAndResult result;
				result.whole = f.get();
				if (result.result <= 0) {
					// Close immediately (???)
					this->hangup(result.fd, (result.result == 0) ? "Disconnected" : "Error");
				}
			}
		}
		futures.clear();
	} // epoll_main_loop

	/* Potentially wait for the last task to finish */
	if (new_fds_future.valid()) {
		new_fds_future.get();
	}
}

std::future<long> EpollServer::handle_new_connections(const std::vector<int>& new_fds, const char* argument)
{
	/* Handle new connections in a VM thread */
	return program().m_storage_queue.enqueue(
	[this, new_fds, argument] () mutable -> long
	{
		auto& storage = *this->program().storage().storage_vm;
		auto func = program().
			entry_at(ProgramEntryIndex::SOCKET_CONNECTED);

		// Fixup argument
		argument = argument ? argument : "";

		for (const int fd : new_fds)
		{
			const int virtual_fd = 0x1000 + fd;
			storage.file_descriptors().manage(fd, virtual_fd);

			const char* peer = "(unknown)";
			if constexpr (false) {
				/* Translate peer sockaddr to string */
				struct sockaddr_in sin;
				socklen_t silen = sizeof(sin);
				getsockname(fd, (struct sockaddr *)&sin, &silen);

				char ip_buffer[INET_ADDRSTRLEN];
				const char *peer =
					inet_ntop(AF_INET, &sin.sin_addr, ip_buffer, sizeof(ip_buffer));
				if (peer == nullptr)
					peer = "(unknown)";
			}

			/* Call the storage VM on_connected callback. */
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				int(virtual_fd), peer, argument);

			/* Get answer from VM, and unmanage the fd if no. */
			const bool answer = storage.machine().return_value();
			if (!answer) {
				storage.file_descriptors().free_byhash(virtual_fd);
			} else {
				/* The virtual machine has final say, regarding managing the fd. */
				this->epoll_add(fd);
			}

		}
		return 0L;
	});
}

bool EpollServer::epoll_add(const int fd)
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

bool EpollServer::manage(const int fd, std::vector<int>& new_fds)
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

	new_fds.push_back(fd);
	return true;
}
void EpollServer::fd_readable(int fd, futures_t& futures)
{
	/* Don't call on_data if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKET_DATA) == 0x0) {
		/* Let's not read anymore. */
		shutdown(fd, SHUT_RD);
		return;
	}

	futures.emplace_back(program().m_storage_queue.enqueue(
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
		ssize_t total = 0;
		while (len > 0 && total < ssize_t(MAX_READ_BUFFER))
		{
			len = readv(fd, (struct iovec *)&buffers[0], n_buffers);
			/* XXX: Possibly very stupid reason to break. But we can always come back. */
			if (len < 0)
				break;
			total += len;

			/* Call the storage VM on_data callback. */
			const int virtual_fd = 0x1000 + fd;
			storage.machine().timed_vmcall(
				func, CALLBACK_TIMEOUT,
				int(virtual_fd),
				m_read_vaddr, ssize_t(len));
		}
		FdAndResult result;
		result.fd = fd;
		result.result = len;
		if (result.result < 0) {
			const bool non_fatal = (len == -1 && errno == EWOULDBLOCK);
			result.result = (non_fatal) ? 1 : -1;
		}
		return result.whole;
	}));
}
void EpollServer::fd_writable(int fd, futures_t& futures)
{
	/* Don't call on_data if the entry is 0x0. */
	if (program().entry_at(ProgramEntryIndex::SOCKET_WRITABLE) == 0x0)
		return;

	futures.emplace_back(program().m_storage_queue.enqueue(
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
		FdAndResult result;
		result.fd = fd;
		result.result = 0;
		return result.whole;
	}));
}

void EpollServer::hangup(int fd, const char *reason)
{
	/* Preemptively close and remove the fd. */
	epoll_ctl(this->m_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);

	auto fut = program().m_storage_queue.enqueue(
	[this, fd, reason] () -> long
	{
		auto& storage = *this->program().storage().storage_vm;
		storage.file_descriptors().free_byval(fd);

		auto func = program().
			entry_at(ProgramEntryIndex::SOCKED_DISCONNECTED);
		/* Don't call on_disconnect if the entry is 0x0. */
		if (func == 0x0)
			return 1;

		const int virtual_fd = 0x1000 + fd;

		/* Call the storage VM on_disconnected callback. */
		storage.machine().timed_vmcall(
			func, CALLBACK_TIMEOUT,
			int(virtual_fd), (const char *)reason);

		return 1;
	});
	fut.get();
}

} // kvm
