#pragma once
#include <thread>
#include "socket_event.hpp"

namespace kvm
{
	class MachineInstance;
	class ProgramInstance;
	class TenantInstance;

	struct EpollServer
	{
		static constexpr size_t MAX_READ_BUFFERS = 32;
		static constexpr size_t MAX_WRITE_BUFFERS = 32;
		static constexpr size_t MAX_VM_WR_BUFFERS = 64;

		EpollServer(const TenantInstance* tenant, ProgramInstance* program, int16_t id);
		~EpollServer();

		const auto& program() const { return *m_program; }
		auto& program() { return *m_program; }

		const auto& tenant() const { return *m_tenant; }
		auto& tenant() { return *m_tenant; }

		const auto& vm() const { return *m_vm; }
		auto& vm() { return *m_vm; }

		void stop();

	private:
		void epoll_main_loop();
		bool manage(int fd, std::vector<SocketEvent>& queue);
		long fd_readable(int fd, std::vector<SocketEvent>&);
		void fd_writable(int fd, std::vector<SocketEvent>&);
		void hangup(int fd, const char *, std::vector<SocketEvent>&);
		bool epoll_add(int fd);
		void resume(const std::vector<SocketEvent>& queue);
		void flush(std::vector<SocketEvent>& queue);

		std::unique_ptr<MachineInstance> m_vm;
		const TenantInstance* m_tenant;
		ProgramInstance* m_program;
		int m_epoll_fd = -1;
		int m_listen_fd = -1;
		int m_event_fd = -1;
		int16_t m_system_id = -1;
		bool m_running = true;
		/* We pre-allocate a reading area. */
		struct ReadBuffer
		{
			uint64_t read_vaddr = 0x0;
			size_t n_buffers = 0;
			std::array<tinykvm::Machine::WrBuffer, MAX_VM_WR_BUFFERS> buffers;
		};
		std::vector<ReadBuffer> m_read_buffers;
		size_t m_current_read_buffer = 0;
		// Translate fd to virtual fd
		std::unordered_map<int, int> m_fd_to_vfd_map;

		std::thread m_epoll_thread;
	};
}
