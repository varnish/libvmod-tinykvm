#pragma once
#include <future>
#include <thread>

namespace kvm
{
	class MachineInstance;
	class ProgramInstance;

	struct EpollServer
	{
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
		bool manage(int fd);
		long fd_readable(int fd);
		void fd_writable(int fd);
		void hangup(int fd, const char *);
		bool epoll_add(int fd);

		struct SocketEvent
		{
			int fd;
			int event;
			uint64_t remote = 0;
			uint64_t arg    = 0;
			uint64_t data = 0;
			size_t data_len = 0;
		};
		void resume(const SocketEvent& se);

		std::unique_ptr<MachineInstance> m_vm;
		const TenantInstance* m_tenant;
		ProgramInstance* m_program;
		int m_epoll_fd = -1;
		int m_listen_fd = -1;
		int m_event_fd = -1;
		int16_t m_system_id = -1;
		bool m_running = true;
		bool m_pause_resume = false;
		/* We pre-allocate a reading area. */
		uint64_t m_read_vaddr = 0x0;
		size_t m_n_buffers = 0;
		tinykvm::Machine::WrBuffer m_buffers[MAX_VM_WR_BUFFERS];

		std::thread m_epoll_thread;
	};
}
