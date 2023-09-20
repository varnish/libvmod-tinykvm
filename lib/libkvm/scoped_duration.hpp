#pragma once
#include <sys/time.h>

namespace kvm
{
	struct ScopedDuration {
		ScopedDuration(double& dest_counter)
			: m_counter(dest_counter), t0(now())  {}
		~ScopedDuration() {
			m_counter += now() - t0;
		}

		static inline double now() noexcept {
			struct timespec ts;

			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
			return (ts.tv_sec + 1e-9 * ts.tv_nsec);
		}

	private:
		double& m_counter;
		const double t0;
	};
} // kvm
