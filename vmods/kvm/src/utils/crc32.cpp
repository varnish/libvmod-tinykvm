#include "crc32.hpp"
#include <x86intrin.h>

namespace kvm
{
	/* Polynomial: 0x1EDC6F41 */
	__attribute__((target("sse4.2")))
	uint32_t crc32c_hw(uint32_t partial, const char* vdata, size_t len)
	{
		const uintptr_t addr = (uintptr_t)vdata;
		const char* alignto = (const char*) ((addr + 0x3) & ~0x3);
		while (vdata < alignto && len > 0) {
			partial = _mm_crc32_u8(partial, *vdata);
			vdata++; len--;
		}
		while (len >= 8) {
			partial = _mm_crc32_u32(partial, *(uint32_t *)(vdata + 0));
			partial = _mm_crc32_u32(partial, *(uint32_t *)(vdata + 4));
			vdata += 8; len -= 8;
		}
		while (len >= 4) {
			partial = _mm_crc32_u32(partial, *(uint32_t *)vdata);
			vdata += 4; len -= 4;
		}
		if (len & 2) {
			partial = _mm_crc32_u16(partial, *(uint16_t *)vdata);
			vdata += 2;
		}
		if (len & 1) {
			partial = _mm_crc32_u8(partial, *vdata);
		}
		return partial;
	}
	__attribute__((target("sse4.2")))
	uint32_t crc32c_hw(const char* data, size_t len)
	{
		return crc32c_hw(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
	}
}
