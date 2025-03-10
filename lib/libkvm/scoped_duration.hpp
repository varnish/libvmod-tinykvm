#pragma once
#include <cstdint>
#include <sys/time.h>

namespace kvm
{
	template <clockid_t CLK = CLOCK_THREAD_CPUTIME_ID>
	struct ScopedDuration {
		ScopedDuration(double& dest_counter)
			: m_counter(dest_counter), t0(now())  {}
		~ScopedDuration() {
			m_counter += now() - t0;
		}

		static inline double now() noexcept {
			struct timespec ts;

			clock_gettime(CLK, &ts);
			return (ts.tv_sec + 1e-9 * ts.tv_nsec);
		}
		static inline uint64_t nanos_now() noexcept {
			struct timespec ts;

			clock_gettime(CLK, &ts);
			return (ts.tv_sec * 1000000000ULL + ts.tv_nsec);
		}

	private:
		double& m_counter;
		const double t0;
	};

	template <clockid_t CLK = CLOCK_REALTIME>
	struct AtomicScopedDuration {
		AtomicScopedDuration(uint64_t& dest_counter)
			: m_counter(dest_counter), t0(now())  {}
		~AtomicScopedDuration() {
			const double duration = now() - t0;
			const uint64_t increment = duration * precision();
			if (increment > 0)
				__sync_fetch_and_add(&m_counter, increment);
		}

		static inline double now() noexcept {
			struct timespec ts;

			clock_gettime(CLK, &ts);
			return (ts.tv_sec + 1e-9 * ts.tv_nsec);
		}

		static constexpr uint64_t precision() {
			return 100 * 1024UL; /* Arbritrary, 10-microsecond precision. */
		}

		static double to_seconds(uint64_t value) {
			return value / double(precision());
		}

	private:
		uint64_t& m_counter;
		const double t0;
	};
} // kvm
