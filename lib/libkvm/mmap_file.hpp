#pragma once
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
		if (m_mmap == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return (uint8_t *)m_mmap;
	}
	size_t size() const
	{
		if (m_mmap == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return m_size;
	}
	bool empty() const noexcept
	{
		return m_mmap == nullptr || m_size == 0;
	}
	std::string_view view() const
	{
		if (m_mmap == nullptr) {
			throw std::runtime_error("MmapFile is not mapped: " + m_filename);
		}
		return std::string_view(static_cast<const char*>(m_mmap), m_size);
	}

	void dontneed()
	{
		// Turn the file into a lazily loaded file
		if (m_mmap != nullptr) {
			if (madvise(m_mmap, m_size, MADV_DONTNEED) < 0) {
				throw std::runtime_error("Failed to advise MADV_DONTNEED on mmap: " + m_filename);
			}
			m_mmap = nullptr;
			m_size = 0;
		}
	}

	std::string filename() const
	{
		return m_filename;
	}

	MmapFile(const std::string& filename);
	MmapFile(const MmapFile& other)
		: MmapFile(other.m_filename) {}
	~MmapFile();

private:
	std::string m_filename;
	void* m_mmap;
	size_t m_size;
};

inline MmapFile::MmapFile(const std::string& filename)
	: m_filename(filename), m_mmap(nullptr), m_size(0)
{
	if (filename.empty()) {
		throw std::invalid_argument("Filename cannot be empty");
	}

	std::filesystem::path path(filename);
	if (!std::filesystem::exists(path)) {
		throw std::runtime_error("File does not exist: " + filename);
	}

	m_size = std::filesystem::file_size(path);
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	m_mmap = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (m_mmap == MAP_FAILED) {
		throw std::runtime_error("Failed to mmap file: " + filename);
	}
}
inline MmapFile::~MmapFile()
{
	if (m_mmap != nullptr) {
		if (munmap(m_mmap, m_size) < 0) {
			perror("Failed to munmap file");
		}
	}
}
