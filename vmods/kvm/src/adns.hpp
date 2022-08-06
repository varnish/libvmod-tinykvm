#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace kvm {

struct AsyncDNSEntry {
	static constexpr size_t HASH_LEN = 32;
	static constexpr size_t ADDR_LEN = 126;

	std::string name;
    uint8_t ipv;
	int8_t  touched;
	uint16_t priority;
	uint16_t weight;
	std::array<uint8_t, HASH_LEN> hash;
};
struct AsyncDNS {
    std::string tag;
    std::mutex  mtx;
    std::vector<AsyncDNSEntry> entries;
};

}