#pragma once
#include <thread>

namespace kvm
{
	class ProgramInstance;

	struct LongLived
	{
		LongLived(std::shared_ptr<ProgramInstance>);
		~LongLived();

		bool manage(int fd, const char *argument);

		const auto& program() const { return *m_prog_ref; }
		auto& program() { return *m_prog_ref; }

	private:
		void epoll_main_loop();
		long fd_readable(int fd);
		void fd_writable(int fd);
		void hangup(int fd, const char *);
		bool epoll_add(int fd);

		int m_epoll_fd = -1;
		bool m_running = true;
		/* We pre-allocate a reading area. */
		uint64_t m_read_vaddr = 0x0;

		std::thread m_epoll_thread;
		std::shared_ptr<ProgramInstance> m_prog_ref;
	};
}
