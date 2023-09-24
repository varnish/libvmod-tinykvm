#include <cstdint>

namespace kvm {

struct MachineStats
{
	uint64_t invocations = 0;
	uint64_t resets      = 0;
	uint64_t exceptions  = 0;
	uint64_t timeouts    = 0;

	double reservation_time = 0;
	double request_cpu_time = 0;
	double error_cpu_time   = 0;

	uint64_t status_2xx = 0;
	uint64_t status_3xx = 0;
	uint64_t status_4xx = 0;
	uint64_t status_5xx = 0;
	uint64_t status_unknown = 0;

	uint64_t input_bytes  = 0;
	uint64_t output_bytes = 0;
};

} // kvm
