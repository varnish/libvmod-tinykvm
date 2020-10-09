#pragma once
#include <libriscv/machine.hpp>
#include <stdexcept>
#include "varnish.hpp"

template <int W>
struct MemArea
{
	using address_t = typename riscv::address_type<W>;

	bool is_within(address_t addr, size_t size) const {
		return addr >= m_begin && addr + size < m_end;
	}
	char* host_addr(address_t addr, size_t size) {
		if (is_within(addr, size)) {
			return &m_data[addr - m_begin];
		}
		return nullptr;
	}

	char* data() { return m_data; }
	const char* data() const { return m_data; }

	MemArea() = default;
	MemArea(riscv::Machine<W>& m, address_t b, address_t e,
		riscv::PageAttributes attr, struct ws* ws)
		: m_begin(b), m_end(e)
	{
		assert(b % riscv::Page::size() == 0);
		assert(e % riscv::Page::size() == 0);
		const size_t bytes = e - b;

		this->m_heap_alloced = (ws == nullptr);
		if (!m_heap_alloced)
			this->m_data = (char*) WS_Alloc(ws, bytes);
		else
			this->m_data = new char[bytes];

		if (this->m_data == nullptr)
			throw std::runtime_error("Unable to allocate memory range of size " + std::to_string(bytes));

		size_t offset = 0;
		b >>= riscv::Page::SHIFT;
		e >>= riscv::Page::SHIFT;
		for (size_t p = b; p < e; p++) {
			m.memory.allocate_page(p, attr, (riscv::PageData*) &m_data[offset]);
			offset += riscv::Page::size();
		}
	}
	~MemArea() {
		if (m_heap_alloced)
			delete this->m_data;
	}

	address_t m_begin = 0;
	address_t m_end   = 0;
	char*     m_data  = nullptr;
	bool      m_heap_alloced = false;
};
