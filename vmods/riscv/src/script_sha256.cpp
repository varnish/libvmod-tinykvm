#include "script_functions.hpp"

extern "C" {
#include <vsha256.h>
}

namespace rvs {

void sha256(machine_t& machine)
{
	auto [buffer, dst, dstlen] =
		machine.sysargs<riscv::Buffer, gaddr_t, unsigned> ();
	if (UNLIKELY(dstlen != 32)) {
		machine.set_result(0);
		return;
	}

	VSHA256_CTX ctx;
	VSHA256_Init(&ctx);
	buffer.foreach(
		[ctx] (const char* data, size_t len) mutable {
			VSHA256_Update(&ctx, data, len);
		});
	unsigned char result[VSHA256_LEN];
	VSHA256_Final(result, &ctx);

	machine.copy_to_guest(dst, result, sizeof(result));
	machine.set_result(VSHA256_LEN);
}

} // rvs
