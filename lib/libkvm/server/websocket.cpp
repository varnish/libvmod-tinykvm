#include "../program_instance.hpp"
#include "../tenant_instance.hpp"
/*
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
*/

namespace kvm {

static constexpr int MAX_EVENTS = 8;
static constexpr size_t MAX_READ_BUFFER = 1UL << 20; /* 1MB */
static constexpr float CALLBACK_TIMEOUT = 8.0f; /* Seconds */
/*
namespace beast = boost::beast;                      // from <boost/beast.hpp>
namespace http = beast::http;                        // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;              // from <boost/beast/websocket.hpp>
namespace net = boost::asio;                         // from <boost/asio.hpp>
using stream_protocol = net::ip::tcp;            	 // from <boost/asio/ip/tcp.hpp>
*/

//------------------------------------------------------------------------------
/*
void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// Echoes back all received WebSocket messages
class session : public std::enable_shared_from_this<session>
{
    websocket::stream<stream_protocol::socket> ws_;
    beast::flat_buffer buffer_;

public:
    // Take ownership of the socket
    explicit
    session(stream_protocol::socket&& socket)
        : ws_(std::move(socket))
    {
    }

    // Get on the correct executor
    void
    run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(ws_.get_executor(),
            beast::bind_front_handler(
                &session::on_run,
                shared_from_this()));
    }

    // Start the asynchronous operation
    void
    on_run()
    {
        // Set suggested timeout settings for the websocket
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res)
            {
                res.set(http::field::server,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-server-async-local");
            }));
        // Accept the websocket handshake
        ws_.async_accept(
            beast::bind_front_handler(
                &session::on_accept,
                shared_from_this()));
    }

    void
    on_accept(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "accept");

        // Read a message
        do_read();
    }

    void
    do_read()
    {
        // Read a message into our buffer
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &session::on_read,
                shared_from_this()));
    }

    void
    on_read(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This indicates that the session was closed
        if(ec == websocket::error::closed)
            return;

        if(ec)
            return fail(ec, "read");

        // Echo the message
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &session::on_write,
                shared_from_this()));
    }

    void
    on_write(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return fail(ec, "write");

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Do another read
        do_read();
    }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    stream_protocol::acceptor acceptor_;

public:
    listener(
        net::io_context& ioc,
        stream_protocol::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(ioc, std::move(endpoint))
    {
    }

    // Start accepting incoming connections
    void
    run()
    {
        do_accept();
    }

private:
    void
    do_accept()
    {
        // The new connection gets its own strand
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &listener::on_accept,
                shared_from_this()));
    }

    void
    on_accept(beast::error_code ec, stream_protocol::socket socket)
    {
        if(ec)
        {
            fail(ec, "accept");
            return;
        }
        else
        {
            // Create the session and run it
            std::make_shared<session>(std::move(socket))->run();
        }

        // Accept another connection
        do_accept();
    }
};
*/
//------------------------------------------------------------------------------

WebSocketThread::WebSocketThread(const TenantInstance* tenant, ProgramInstance* prog,
	int16_t id, io_context_t& io_context)
	: m_tenant(tenant),
	  m_program(prog),
	  m_system_id(id),
	  m_io_context(io_context)
{
	if (!prog->has_storage()) {
		throw std::runtime_error("WebSocketThread requires a storage VM");
	}

	std::string address = "127.0.0.1";
	const uint16_t port = tenant->config.group.ws_server_port;
	if (!tenant->config.group.ws_server_address.empty()) {
		// Use the address from the configuration
		address = tenant->config.group.ws_server_address;
	}

	// Set the server to running state
	this->m_running = true;
	if (this->m_system_id == 0) // Only print once
		printf("%s server started on '%s' vm=%u, nodes=1, huge=%d/%d\n",
			"WebSocket",
			address.c_str(),
			tenant->config.group.epoll_systems,
			tenant->config.group.hugepage_arena_size > 0,
			tenant->config.group.hugepage_requests_arena > 0);

	// Create a VM instance for the epoll server, by forking the program main VM
	this->m_vm = std::make_unique<MachineInstance>(
		this->m_system_id, *this->m_program->main_vm, this->m_tenant, this->m_program);
	// Create a read buffer for the VM
	this->m_read_vaddr = this->vm().allocate_post_data(MAX_READ_BUFFER);
	if (this->m_read_vaddr == 0) {
		throw std::runtime_error("Failed to allocate read buffer");
	}
	/* Gather buffers from writable area */
	this->m_n_buffers =
		vm().machine().writable_buffers_from_range(
			this->m_buffers, this->m_read_vaddr, MAX_READ_BUFFER);

	// Set up the paused VM state if the pause-resume API is enabled
	const auto pause_resume_entry = this->m_program->entry_at(ProgramEntryIndex::SOCKET_PAUSE_RESUME_API);
	if (pause_resume_entry != 0x0)
	{
		// Call the pause-resume API to set up the VM state, which *must*
		// report that the VM is waiting for requests.
		this->m_vm->reset_wait_for_requests();

		this->m_vm->machine().timed_vmcall(pause_resume_entry, CALLBACK_TIMEOUT, this->m_system_id);

		if (this->m_vm->is_waiting_for_requests()) {
			this->m_pause_resume = true;
			// Skip over the OUT instruction (from waiting for requests)
			auto& regs = this->m_vm->machine().registers();
			regs.rip += 2;
			this->m_vm->machine().set_registers(regs);
		} else {
			fprintf(stderr, "WebSocketThread: VM is not waiting for requests\n");
			throw std::runtime_error("VM is not waiting for requests");
		}
	}

	// Start the I/O thread
	this->m_io_thread = std::thread(&WebSocketThread::ws_main_loop, this);
}
void WebSocketThread::stop()
{
	this->m_running = false;
	m_io_thread.join();
}
WebSocketThread::~WebSocketThread()
{
	if (this->m_running) {
		this->stop();
	}
}

void WebSocketThread::wait()
{
	if (this->m_io_thread.joinable()) {
		this->m_io_thread.join();
	}
}
void WebSocketThread::ws_main_loop()
{
	//this->m_io_context.run();
}


WebSocketServer::WebSocketServer(const TenantInstance* tenant, ProgramInstance* program, size_t threads)
	: m_tenant(tenant),
	  m_program(program)
{
	if (threads == 0) {
		throw std::runtime_error("WebSocketServer requires at least one thread");
	}
	const uint16_t port = tenant->config.group.ws_server_port;
	if (port == 0) {
		throw std::runtime_error("WebSocketServer requires a valid port");
	}
/*
	this->m_io_context = std::make_unique<net::io_context>(threads);

	// Create the WebSocket threads
	for (size_t i = 0; i < threads; ++i) {
		this->m_threads.emplace_back(tenant, program, static_cast<int16_t>(i),
			*this->m_io_context);
	}

	// Create the listener for the WebSocket server
	std::make_shared<listener>(*this->m_io_context,
		stream_protocol::endpoint({stream_protocol::v4(), port}))->run();
*/
	printf("%s server started on '%s' vms=%zu, nodes=%d, huge=%d/%d\n",
		"WebSocket",
		tenant->config.group.ws_server_address.c_str(),
		this->m_threads.size(),
		1,
		tenant->config.group.hugepage_arena_size > 0,
		tenant->config.group.hugepage_requests_arena > 0);

	// Wait for the threads
	for (auto& thread : this->m_threads) {
		thread.wait();
	}
}
void WebSocketServer::stop()
{
	for (auto& thread : m_threads) {
		thread.stop();
	}
	this->m_running = false;
}

WebSocketServer::~WebSocketServer()
{
	if (this->m_running) {
		this->stop();
	}
}

} // namespace kvm
