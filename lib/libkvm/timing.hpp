#pragma once
#include <mutex>
#include <vector>
#include <numeric>
#include <ctime>

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = kvm::time_now();  \
	asm("" ::: "memory");

namespace kvm {

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);

struct Timing {
	static const size_t MEASUREMENTS = 3000;
	std::vector<long> vec;
	std::mutex mtx;
	std::string description;

	void add(timespec t0, timespec t1, bool do_print = true);

	Timing(const std::string& descr)
		: description(descr) { vec.reserve(MEASUREMENTS); }
};

inline void Timing::add(timespec t0, timespec t1, bool do_print)
{
	std::vector<long> copy;
	const long delta = nanodiff(t0, t1);
	mtx.lock();
	vec.push_back(delta);
	do_print = do_print && (vec.size() == MEASUREMENTS);
	if (do_print) copy = std::move(vec);
	mtx.unlock();

	if (do_print) {
		long min = *min_element(copy.begin(), copy.end());
		long max = *max_element(copy.begin(), copy.end());
		long total = std::accumulate(copy.begin(), copy.end(), 0l);
		long median = copy.at(copy.size() / 2);
		printf("Timing result [%s] %zu samples: avg=%ldns, median=%ldns, min=%ldns, max=%ldns\n",
			description.c_str(), MEASUREMENTS, total / MEASUREMENTS, median, min, max);
	}
}


timespec time_now()
{
	timespec t;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t;
}
long nanodiff(timespec start_time, timespec end_time)
{
	long nsec = (end_time.tv_sec - start_time.tv_sec) * 1000000000;
	return nsec + end_time.tv_nsec - start_time.tv_nsec;
}

} // kvm
