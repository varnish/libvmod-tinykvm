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
static constexpr float CALLBACK_TIMEOUT = 8.0f; /* Seconds */

EpollServer::EpollServer(const TenantInstance* tenant, ProgramInstance* prog, int16_t id)
	: m_tenant(tenant),
	  m_program(prog),
	  m_system_id(id)
{
	if (!prog->has_storage()) {
		throw std::runtime_error("EpollServer requires a storage VM");
	}

	this->m_epoll_fd = epoll_create(MAX_EVENTS);
	std::string address;

	const bool is_unix = tenant->config.group.server_port == 0;

	// Create a listening UNIX socket
	this->m_listen_fd = socket(is_unix ? AF_UNIX : AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (this->m_listen_fd < 0) {
		throw std::runtime_error("Failed to create socket");
	}
	// Set the socket to reuse the address
	int opt = 1;
	if (setsockopt(this->m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(this->m_listen_fd);
		throw std::runtime_error("Failed to set socket options");
	}

	if (is_unix)
	{
		address = tenant->config.group.server_address;
		if (address.empty()) {
			throw std::runtime_error("Invalid server address: '" + address + "'");
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
	}
	else
	{
		// Set the socket to reuse the port (IPv4 and IPv6 only) for load-balancing
		if (setsockopt(this->m_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
			fprintf(stderr, "WARNING: Failed to set SO_REUSEPORT: %s\n", strerror(errno));
		}

		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%s:%d",
			"0.0.0.0",
			tenant->config.group.server_port);
		address = buffer;

		// Set up bind to 127.0.0.1:port
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(tenant->config.group.server_port);
		addr.sin_addr = in_addr{INADDR_ANY}; // Listen on all interfaces

		// Bind the socket to the address
		if (bind(this->m_listen_fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
			close(this->m_listen_fd);
			throw std::runtime_error("Failed to bind socket");
		}
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
	if (this->m_system_id == 0) // Only print once
		printf("%s epoll server started on '%s' vm=%u, nodes=1, huge=%d/%d\n",
			is_unix ? "Unix" : "IPv4",
			address.c_str(),
			tenant->config.group.epoll_systems,
			tenant->config.group.hugepage_arena_size > 0,
			tenant->config.group.hugepage_requests_arena > 0);

	// Create a VM instance for the epoll server, by forking the program main VM
	this->m_vm = std::make_unique<MachineInstance>(
		this->m_system_id, *this->m_program->main_vm, this->m_tenant, this->m_program);

	// Set up the paused VM state if the pause-resume API is enabled
	size_t needed_buffers = 1;
	const auto pause_resume_entry = this->m_program->entry_at(ProgramEntryIndex::SOCKET_PAUSE_RESUME_API);
	if (pause_resume_entry != 0x0)
	{
		// Call the pause-resume API to set up the VM state, which *must*
		// report that the VM is waiting for requests.
		this->m_vm->reset_wait_for_requests();

		this->m_vm->machine().timed_vmcall(pause_resume_entry, CALLBACK_TIMEOUT, this->m_system_id);

		if (this->m_vm->is_waiting_for_requests()) {
			// Skip over the OUT instruction (from waiting for requests)
			auto& regs = this->m_vm->machine().registers();
			regs.rip += 2;
			this->m_vm->machine().set_registers(regs);
		} else {
			fprintf(stderr, "EpollServer: VM is not waiting for requests\n");
			throw std::runtime_error("VM is not waiting for requests");
		}

		// The number of buffers is in the second argument of the
		// pause-resume API, which is in the rsi register.
		needed_buffers = this->m_vm->machine().registers().rsi;
		needed_buffers = std::clamp(needed_buffers, size_t(1), MAX_READ_BUFFERS);
	} else {
		throw std::runtime_error("EpollServer: Pause-resume API not waiting for requests");
	}

	// Allocate the buffers for the VM in a single chunk
	const uint64_t allocation = this->m_vm->allocate_post_data(MAX_READ_BUFFER * needed_buffers);
	if (allocation == 0) {
		throw std::runtime_error("Failed to allocate read buffers");
	}
	this->m_read_buffers.resize(needed_buffers);
	for (size_t i = 0; i < needed_buffers; ++i)
	{
		auto& rb = this->m_read_buffers[i];
		rb.read_vaddr = allocation + (i * MAX_READ_BUFFER);
		/* Gather buffers from writable area */
		rb.n_buffers =
			vm().machine().writable_buffers_from_range(rb.buffers.size(),
				rb.buffers.data(), rb.read_vaddr, MAX_READ_BUFFER);
	}

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
	std::vector<SocketEvent> queue;

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
						if (!this->manage(fd, queue)) {
							fprintf(stderr, "epoll manage error: %s\n", strerror(errno));
							close(fd);
						}
					}
					continue; /* Move to next event */
				}
				else if (UNLIKELY(ev.data.fd == this->m_event_fd))
				{
					/* Eventfd triggered, stop the server */
					uint64_t u;
					eventfd_read(this->m_event_fd, &u);
					this->m_running = false;
					continue; /* Move to next event */
				}
				try {
					const ssize_t res = this->fd_readable(ev.data.fd, queue);
					if (res <= 0) {
						const bool non_fatal = res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
						if (non_fatal) {
							/* Ignore EAGAIN and EWOULDBLOCK */
							continue;
						}
						// Close immediately (???)
						this->hangup(ev.data.fd, (res == 0) ? "Disconnected" : "Error", queue);
						continue; // Move to next event
					}
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll read exception: %s\n", e.what());
				}
			}
			if (ev.events & EPOLLOUT)
			{
				try {
					this->fd_writable(ev.data.fd, queue);
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll write exception: %s\n", e.what());
				}
			}
			if (ev.events & EPOLLHUP)
			{
				try {
					this->hangup(ev.data.fd, "Hangup", queue);
				} catch (const std::exception& e) {
					/* XXX: Implement me. */
					fprintf(stderr, "epoll hangup exception: %s\n", e.what());
				}
			}
		} // nfds
		this->flush(queue);
	} // epoll_main_loop
}
void EpollServer::flush(std::vector<SocketEvent>& queue)
{
	if (!queue.empty())
	{
		/* Resume the VM with the queued events */
		this->resume(queue);
		queue.clear();
	}
	this->m_current_read_buffer = 0;
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

bool EpollServer::manage(const int fd, std::vector<SocketEvent>& queue)
{
	/* Make non-blocking */
	int r = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (r < 0)
		return false;

	/* Enable TCP keepalive */
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));

	/* Register the fd with the VM */
	const int virtual_fd = 0x1000 + fd;
	vm().machine().fds().manage(fd, virtual_fd);

	const char* argument = "";

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

	bool answer = false;
	/* Steal a read-buffer for argument and peer. */
	auto& rb = this->m_read_buffers.at(m_current_read_buffer);
	m_current_read_buffer++;

	SocketEvent se;
	se.fd = virtual_fd;
	se.event = 0; // SOCKET_CONNECTED
	__u64 stack = rb.read_vaddr + 0x1000;
	se.remote = vm().machine().stack_push_cstr(stack, peer);
	se.arg = vm().machine().stack_push_cstr(stack, argument);
	queue.push_back(se);
	// There is currently no good way to check if the VM accepted the fd.
	answer = true;

	if (!answer) {
		vm().machine().fds().free(virtual_fd);
		return false;
	} else {
		/* The virtual machine has final say, regarding managing the fd. */
		return this->epoll_add(fd);
	}
}
long EpollServer::fd_readable(int fd, std::vector<SocketEvent>& queue)
{
	ssize_t len = MAX_READ_BUFFER;
	ssize_t total = 0;
	while (len > 0 && total < ssize_t(MAX_READ_BUFFER))
	{
		if (this->m_current_read_buffer >= this->m_read_buffers.size())
		{
			this->flush(queue);
		}
		auto& rb = this->m_read_buffers.at(m_current_read_buffer);
		len = readv(fd, (struct iovec *)&rb.buffers[0], rb.n_buffers);
		/* XXX: Possibly very stupid reason to break. But we can always come back. */
		if (len <= 0)
			break;
		total += len;

		/* Call the storage VM on_data callback. */
		const int virtual_fd = 0x1000 + fd;
		SocketEvent se;
		se.fd = virtual_fd;
		se.event = 2; // SOCKET_WRITABLE
		se.data = rb.read_vaddr;
		se.data_len = len;
		m_current_read_buffer++;
		queue.push_back(se);
	}
	return len;
}
void EpollServer::fd_writable(int fd, std::vector<SocketEvent>& queue)
{
	const int virtual_fd = 0x1000 + fd;

	SocketEvent se;
	se.fd = virtual_fd;
	se.event = 2; // SOCKET_WRITABLE
	queue.push_back(se);
}

void EpollServer::hangup(int fd, const char *reason, std::vector<SocketEvent>& queue)
{
	/* Preemptively close and remove the fd. */
	epoll_ctl(this->m_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);

	const int virtual_fd = 0x1000 + fd;
	vm().machine().fds().free(virtual_fd);

	SocketEvent se;
	se.fd = virtual_fd;
	se.event = 3; // SOCKET_DISCONNECTED
	queue.push_back(se);
}

void EpollServer::resume(const std::vector<SocketEvent>& se)
{
	/* Copy the SocketEvent to the VM, address is already in RDI. */
	auto& regs = vm().machine().registers();
	vm().machine().copy_to_guest(regs.rdi, se.data(), se.size() * sizeof(SocketEvent));
	regs.rax = se.size();
	vm().machine().set_registers(regs);

	/* Resume the VM */
	vm().reset_wait_for_requests();
	vm().machine().vmresume(CALLBACK_TIMEOUT);

	/* Check if the VM is *again* waiting for requests */
	if (!vm().is_waiting_for_requests()) {
		throw std::runtime_error("EpollServer: VM is not waiting for requests");
	}
	/* Skip over the OUT instruction (from waiting for requests) */
	regs.rip += 2;
	vm().machine().set_registers(regs);
}

void syscall_sockets_write(tinykvm::vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	const uint64_t g_evs = regs.rdi;
	const size_t   g_cnt = regs.rsi;

	std::size_t cnt = std::clamp(g_cnt, size_t(1), EpollServer::MAX_WRITE_BUFFERS);
	// Directly view the memory as an array of SocketEvent, or throw an exception
	SocketEvent* events = cpu.machine().writable_memarray<SocketEvent>(g_evs, cnt);

	// Process the write events efficiently using a single syscall
	std::array<tinykvm::Machine::Buffer, 256> buffers;

	for (size_t i = 0; i < cnt; ++i)
	{
		auto& se = events[i];
		if (se.event == 2) // SOCKET_WRITABLE
		{
			// Gather buffers from guest memory. This call never fails (throws instead).
			const size_t n = cpu.machine().gather_buffers_from_range(
				buffers.size(), &buffers[0], se.data, se.data_len);
			// And then we can writev to the socket immediately
			const int fd = cpu.machine().fds().translate(se.fd);
			const ssize_t written = writev(fd, (struct iovec *)&buffers[0], n);
			if (written < 0) {
				const int error_val = errno;
				if (error_val == EAGAIN || error_val == EWOULDBLOCK) {
					continue;
				}
				se.fd = -error_val;
				// Handle the error (e.g., log it, set an error code, etc.)
				fprintf(stderr, "Error writing to socket %d: %s\n", fd, strerror(error_val));
				continue;
			}
			// Store the number of bytes written in the first member of SocketEvent
			se.fd = written;
		}
	}
}

} // kvm
