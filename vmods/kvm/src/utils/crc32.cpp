#include "crc32.hpp"
#include <x86intrin.h>

namespace kvm
{
	/* Polynomial: 0x1EDC6F41 */
	__attribute__((target("sse4.2")))
	uint32_t crc32c_hw(const char* vdata, size_t len)
	{
		uint32_t hash = 0xFFFFFFFF;
		const uintptr_t addr = (uintptr_t)vdata;
		const char* alignto = (const char*) ((addr + 0x3) & ~0x3);
		while (vdata < alignto && len > 0) {
			hash = _mm_crc32_u8(hash, *vdata);
			vdata++; len--;
		}
		while (len >= 8) {
			hash = _mm_crc32_u32(hash, *(uint32_t *)(vdata + 0));
			hash = _mm_crc32_u32(hash, *(uint32_t *)(vdata + 4));
			vdata += 8; len -= 8;
		}
		while (len >= 4) {
			hash = _mm_crc32_u32(hash, *(uint32_t *)vdata);
			vdata += 4; len -= 4;
		}
		if (len & 2) {
			hash = _mm_crc32_u16(hash, *(uint16_t *)vdata);
			vdata += 2;
		}
		if (len & 1) {
			hash = _mm_crc32_u8(hash, *vdata);
		}
		return hash ^ 0xFFFFFFFF;
	}
}
