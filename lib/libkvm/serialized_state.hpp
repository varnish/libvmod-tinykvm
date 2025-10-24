#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace kvm {
enum class ProgramEntryIndex : uint8_t {
	ON_RECV = 0,
	BACKEND_GET  = 1,
	BACKEND_POST = 2,
	BACKEND_METHOD = 3,
	BACKEND_STREAM = 4,
	BACKEND_ERROR  = 5,
	LIVEUPD_SERIALIZE = 6,
	LIVEUPD_DESERIALIZE = 7,

	SOCKET_PAUSE_RESUME_API = 12,

	TOTAL_ENTRIES
};

struct SerializedState {
	/* Entry points in the tenants program. Handlers for all types of
	   requests, serialization mechanisms and related functionality.
	   NOTE: Limiting the entries to lower 32-bits, for now. */
	std::array<uint32_t, (size_t)ProgramEntryIndex::TOTAL_ENTRIES> entry_address {};
};
}