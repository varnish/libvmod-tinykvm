#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

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
	char *text;
	for (size_t i = 0; i < 100; i++) {
		text = malloc(4000);
	}
	volatile double f = 1.0;
	f = pow(f, 2.0);
	sprintf(text, "Complex result: %s, %f", arg, f);
	return_result("text/complex", text);
}
