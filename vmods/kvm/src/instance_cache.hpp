#pragma once
#include <vector>
#include <stdexcept>

namespace kvm {
/**
 * @brief Manage cached values for tenant programs
 * 
 * 
 * It is not necessary to use hash, but it should be non-zero when managing new objects.
 * When hash is not used, it is a stand-in for a used/free value indicator.
**/

template <typename T>
struct Cache {
	struct Entry {
		T        item;
		uint32_t hash = 0;
		bool     non_owned = false;

		bool empty() const noexcept { return hash == 0; }
		void free() { item = T(); hash = 0; }
	};

	auto& get(size_t idx) {
		return cache.at(idx);
	}
	T item(size_t idx) {
		return cache.at(idx).item;
	}
	T translate(uint32_t hash) {
		for (unsigned idx = 0; idx < cache.size(); idx++) {
			if (cache[idx].hash == hash) return cache[idx].item;
		}
		return -1;
	}
	int find(uint32_t hash) {
		for (unsigned idx = 0; idx < cache.size(); idx++) {
			if (cache[idx].hash == hash) return idx;
		}
		return -1;
	}

	size_t max_entries() const noexcept {
		return cache.capacity();
	}
	bool is_full() const noexcept {
		return cache.size() >= max_entries();
	}
	size_t manage(const T& ptr, uint32_t hash)
	{
		// Add new slot
		if (cache.size() < max_entries())
		{
			cache.push_back({ptr, hash});
			return cache.size() - 1;
		}
		// Re-use existing slot
		for (unsigned idx = 0; idx < cache.size(); idx++) {
			if (cache[idx].empty()) {
				cache[idx] = {ptr, hash};
				return idx;
			}
		}
		throw std::out_of_range("Too many items in cache: " + std::string(description));
	}
	void free(size_t idx)
	{
		cache.at(idx).free();
	}
	bool free_byhash(uint32_t hash)
	{
		for (auto& entry : cache) {
			if (entry.hash == hash) {
				entry.free();
				return true;
			}
		}
		return false;
	}
	bool free_byval(const T& val)
	{
		for (auto& entry : cache) {
			if (!entry.empty() && entry.item == val) {
				entry.free();
				return true;
			}
		}
		return false;
	}

	void reset_and_loan(const Cache& other) {
		/* Clear out the cache and reset to other */
		cache = {};
		cache.reserve(other.max_entries());

		/* Load the items of the other and make them non-owned */
		for (const auto& item : other.cache) {
			cache.push_back(item);
			cache.back().non_owned = true;
		}
	}
	void foreach_owned(std::function<void(Entry&)> callback) {
		for (auto& entry : cache) {
			if (entry.hash != 0 && !entry.non_owned)
				callback(entry);
		}
	}

	Cache(size_t max, const char* desc)
		: description(desc) { cache.reserve(max); }

	std::vector<Entry> cache;
	const char* description;
};

} // kvm
