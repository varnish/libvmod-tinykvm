#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sys/mman.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>

struct MmapFile
{
	const uint8_t* data() const
	{
		if (m_mapping == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return (const uint8_t *)m_mapping->m_mmap;
	}
	size_t size() const
	{
		if (m_mapping == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return m_mapping->m_size;
	}
	bool empty() const noexcept
	{
		return m_mapping == nullptr || m_mapping->m_size == 0;
	}
	std::string_view view() const
	{
		if (m_mapping == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return std::string_view(static_cast<const char*>(m_mapping->m_mmap), m_mapping->m_size);
	}

	void dontneed()
	{
		// Turn the file into a lazily loaded file
		if (m_mapping != nullptr) {
			if (madvise(m_mapping->m_mmap, m_mapping->m_size, MADV_DONTNEED) < 0) {
				throw std::runtime_error("Failed to advise MADV_DONTNEED on mmap: " + m_filename);
			}
		}
	}

	const std::string& filename() const
	{
		return m_filename;
	}

	MmapFile(const std::string& filename);
	MmapFile(const MmapFile& other) = default;
	MmapFile& operator=(const MmapFile& other) = default;
	~MmapFile() = default;

private:
	struct SharedMapping {
		SharedMapping(void* mmap, size_t size)
			: m_mmap(mmap), m_size(size) {}
		~SharedMapping() {
			if (m_mmap != nullptr) {
				munmap(m_mmap, m_size);
			}
		}
		void* m_mmap;
		size_t m_size;
	};
	mutable std::shared_ptr<SharedMapping> m_mapping;
	std::string m_filename;
};

inline MmapFile::MmapFile(const std::string& filename)
	: m_mapping(nullptr),
	  m_filename(filename)
{
	if (filename.empty()) {
		throw std::invalid_argument("Filename cannot be empty");
	}

	std::filesystem::path path(filename);
	if (!std::filesystem::exists(path)) {
		throw std::runtime_error("File does not exist: " + filename);
	}

	const size_t size = std::filesystem::file_size(path);
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	void *addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (addr == MAP_FAILED) {
		throw std::runtime_error("Failed to mmap file: " + filename);
	}

	this->m_mapping = std::make_shared<SharedMapping>(addr, size);
}
