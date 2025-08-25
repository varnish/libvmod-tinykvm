#pragma once
#include "mmap_file.hpp"
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

struct BinaryStorage
{
	void set_binary(std::vector<uint8_t> binary);
	void set_binary(const std::string& filepath);
	bool empty() const noexcept;
	const uint8_t* data() const;
	size_t size() const noexcept;

	std::span<const uint8_t> binary() const;
	std::vector<uint8_t> to_vector() const;
	void dontneed();

	BinaryStorage();
	BinaryStorage(std::vector<uint8_t> binary)
		: m_binary(std::move(binary)) {}
	BinaryStorage(const std::string& filepath)
		: m_binary(MmapFile(filepath)) {}
	BinaryStorage(const BinaryStorage& other);
	~BinaryStorage() = default;

private:
	std::variant<std::vector<uint8_t>, MmapFile> m_binary;
};

inline void BinaryStorage::set_binary(std::vector<uint8_t> binary)
{
	m_binary = std::move(binary);
}
inline void BinaryStorage::set_binary(const std::string& filepath)
{
	m_binary = MmapFile(filepath);
}
inline bool BinaryStorage::empty() const noexcept
{
	switch (m_binary.index()) {
	case 0: return std::get<std::vector<uint8_t>>(m_binary).empty();
	case 1: return std::get<MmapFile>(m_binary).empty();
	default: return true;
	}
}
inline const uint8_t* BinaryStorage::data() const
{
	switch (m_binary.index()) {
	case 0: return std::get<std::vector<uint8_t>>(m_binary).data();
	case 1: return std::get<MmapFile>(m_binary).data();
	default: return nullptr;
	}
}
inline size_t BinaryStorage::size() const noexcept
{
	switch (m_binary.index()) {
	case 0: return std::get<std::vector<uint8_t>>(m_binary).size();
	case 1: return std::get<MmapFile>(m_binary).size();
	default: return 0;
	}
}
inline std::span<const uint8_t> BinaryStorage::binary() const
{
	switch (m_binary.index()) {
	case 0: return std::span<const uint8_t>(std::get<std::vector<uint8_t>>(m_binary));
	case 1: {
		const MmapFile& mmap = std::get<MmapFile>(m_binary);
		return std::span<const uint8_t>(mmap.data(), mmap.size());
	}
	default: return {};
	}
}
inline std::vector<uint8_t> BinaryStorage::to_vector() const
{
	switch (m_binary.index()) {
	case 0: return std::get<std::vector<uint8_t>>(m_binary);
	case 1: {
		const MmapFile& mmap = std::get<MmapFile>(m_binary);
		return std::vector<uint8_t>(mmap.data(), mmap.data() + mmap.size());
	}
	default: return {};
	}
}
inline void BinaryStorage::dontneed()
{
	switch (m_binary.index()) {
	case 0: break; // No-op for vector
	case 1: std::get<MmapFile>(m_binary).dontneed(); break;
	}
}

inline BinaryStorage::BinaryStorage()
	: m_binary(std::vector<uint8_t>())
{
}
inline BinaryStorage::BinaryStorage(const BinaryStorage& other)
	: m_binary(other.m_binary)
{
}
