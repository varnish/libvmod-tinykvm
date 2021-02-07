#include <EASTL/fixed_vector.h>

template <typename T>
struct Cache {
	struct Entry {
		T*       item   = nullptr;
		uint32_t hash = 0;
		bool     non_owned = false;
	};

	T* get(size_t idx) {
		return cache.at(idx).item;
	}
	int find(uint32_t hash) {
		for (unsigned idx = 0; idx < cache.size(); idx++) {
			if (cache[idx].hash == hash) return idx;
		}
		return -1;
	}
	size_t manage(T* ptr, uint32_t hash)
	{
		if (cache.size() < max_entries)
		{
			cache.push_back({ptr, hash});
			return cache.size() - 1;
		}
		throw std::out_of_range("Too many cached items");
	}
	void free(size_t idx)
	{
		cache.at(idx) = { nullptr, 0 };
	}

	void loan_from(const Cache& source) {
		/* Load the items of the source and make them non-owned */
		for (const auto& item : source.cache) {
			cache.push_back(item);
			cache.back().non_owned = true;
		}
	}
	void foreach_owned(std::function<void(Entry&)> callback) {
		/* Load the items of the source and make them non-owned */
		for (auto& entry : cache) {
			if (entry.item && !entry.non_owned)
				callback(entry);
		}
	}

	Cache(size_t max) : max_entries(max) {}

	eastl::vector<Entry> cache;
	const size_t max_entries;
};
