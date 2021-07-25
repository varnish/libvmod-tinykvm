#include <stdint.h>

__attribute__((naked, noinline, noreturn))
void sys_return_result(const char *ctype, uint64_t tlen, const char *content, uint64_t clen)
{
	asm("movw $0xFFFF, %%ax; outl %%eax, $0;\n" : : : "eax", "memory");
	__builtin_unreachable();
}

void return_result(const char *ctype, const char *content)
{
	sys_return_result(ctype, strlen(ctype), content, strlen(content));
}
