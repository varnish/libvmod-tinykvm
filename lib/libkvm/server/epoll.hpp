#pragma once
#include <future>
#include <thread>

namespace kvm
{
	class ProgramInstance;

	struct EpollServer
	{
		EpollServer(const TenantInstance* tenant, ProgramInstance* program, int16_t id);
		~EpollServer();

		const auto& program() const { return *m_program; }
		auto& program() { return *m_program; }

		const auto& tenant() const { return *m_tenant; }
		auto& tenant() { return *m_tenant; }

		void stop();

	private:
		union FdAndResult
		{
			long whole = 0;
			struct {
				int fd;
				int result;
			};

			constexpr FdAndResult() : whole(0) {}
			FdAndResult(long whole) : whole(whole) {}
			FdAndResult(int fd, int result) : fd(fd), result(result) {}
		};
		using futures_t = std::vector<std::future<long>>;
		void epoll_main_loop();
		bool manage(int fd, std::vector<int>& new_fds);
		std::future<long> handle_new_connections(const std::vector<int>& new_fds, const char* argument);
		void fd_readable(int fd, futures_t& futures);
		void fd_writable(int fd, futures_t& futures);
		void hangup(int fd, const char *);
		bool epoll_add(int fd);

		int m_epoll_fd = -1;
		int m_listen_fd = -1;
		int m_event_fd = -1;
		int16_t m_system_id = -1;
		bool m_running = true;
		/* We pre-allocate a reading area. */
		uint64_t m_read_vaddr = 0x0;

		std::thread m_epoll_thread;
		const TenantInstance* m_tenant;
		ProgramInstance* m_program;
	};
}
