#pragma once
#include <cstddef>
#include <cstdint>

namespace kvm
{
	struct SocketEvent
	{
		int fd;
		int event;
		uint64_t remote = 0;
		uint64_t arg    = 0;
		uint64_t data = 0;
		size_t data_len = 0;
	};
}
