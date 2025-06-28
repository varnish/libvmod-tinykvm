#pragma once
#include <deque>
#include <thread>
#include "socket_event.hpp"
/*
#include <boost/beast.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
*/

namespace kvm
{
	class MachineInstance;
	class ProgramInstance;
	class TenantInstance;

	struct WebSocketThread
	{
		//using io_context_t = boost::asio::io_context;
		using io_context_t = int;
		static constexpr size_t MAX_VM_WR_BUFFERS = 64;
		WebSocketThread(const TenantInstance* tenant, ProgramInstance* program, int16_t id,
			io_context_t& io_context);
		~WebSocketThread();

		const auto& program() const { return *m_program; }
		auto& program() { return *m_program; }

		const auto& tenant() const { return *m_tenant; }
		auto& tenant() { return *m_tenant; }

		const auto& vm() const { return *m_vm; }
		auto& vm() { return *m_vm; }

		void wait();
		void stop();

	private:
		void ws_main_loop();
		void resume(const SocketEvent& se);

		std::unique_ptr<MachineInstance> m_vm;
		const TenantInstance* m_tenant;
		ProgramInstance* m_program;
		int16_t m_system_id = -1;
		bool m_running = true;
		bool m_pause_resume = false;
		/* We pre-allocate a reading area. */
		uint64_t m_read_vaddr = 0x0;
		size_t m_n_buffers = 0;
		std::vector<tinykvm::Machine::WrBuffer> m_buffers;

		std::thread m_io_thread;
		io_context_t& m_io_context;
	};

	struct WebSocketServer
	{
		//using io_context_t = boost::asio::io_context;
		using io_context_t = int;
		WebSocketServer(const TenantInstance* tenant, ProgramInstance* program, size_t threads);
		~WebSocketServer();

		const auto& program() const { return *m_program; }
		auto& program() { return *m_program; }

		const auto& tenant() const { return *m_tenant; }
		auto& tenant() { return *m_tenant; }

		void stop();

	private:
		void ws_main_loop();
		void resume(const SocketEvent& se);

		const TenantInstance* m_tenant;
		ProgramInstance* m_program;
		bool m_running = true;
		std::unique_ptr<io_context_t> m_io_context;

		std::deque<WebSocketThread> m_threads;
	};
}
