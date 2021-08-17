#pragma once
#include <vector>
#include <stdexcept>

namespace kvm {

template <typename T>
struct Cache {
	struct Entry {
		T        item;
		uint32_t hash = 0;
		bool     non_owned = false;

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
	size_t manage(T& ptr, uint32_t hash)
	{
		// Add new slot
		if (cache.size() < max_entries)
		{
			cache.push_back({ptr, hash});
			return cache.size() - 1;
		}
		// Re-use existing slot
		for (unsigned idx = 0; idx < cache.size(); idx++) {
			if (cache[idx].item == T()) {
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

	void reset_and_loan(const Cache& other) {
		/* Clear out the cache and reset to other */
		this->max_entries = other.max_entries;
		cache.clear();
		/* Load the items of the other and make them non-owned */
		for (const auto& item : other.cache) {
			cache.push_back(item);
			cache.back().non_owned = true;
		}
	}
	void foreach_owned(std::function<void(Entry&)> callback) {
		for (auto& entry : cache) {
			if (entry.item && !entry.non_owned)
				callback(entry);
		}
	}

	Cache(size_t max, const char* desc)
		: max_entries(max), description(desc) {}

	std::vector<Entry> cache;
	size_t max_entries;
	const char*  description;
};

} // kvm
