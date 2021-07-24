#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	printf("Hello from %s!\n", argv[1]);
}

__attribute__((naked, noinline, noreturn))
void sys_return_result(const char *ctype, uint64_t tlen, const char *content, uint64_t clen)
{
	asm("movw $0xFFFF, %%ax; out %%ax, $0;\n" : : : "eax", "memory");
	__builtin_unreachable();
}

void return_result(const char *ctype, const char *content)
{
	sys_return_result(ctype, strlen(ctype), content, strlen(content));
}

__attribute__((used))
extern void my_backend(const char *arg)
{
	return_result("text/plain", arg);
}
