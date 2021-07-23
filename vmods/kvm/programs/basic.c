#include <stdint.h>
#include <stdio.h>

int main()
{
	/* Maybe print something here */
	printf("Hello Basic KVM world!\n");
}

__attribute__((naked, noreturn))
void return_result(const char *ctype, const char *content)
{
	asm("\
	movl $60, %eax\n\
	out %eax, %dx\n\
	");
}

__attribute__((used))
extern void my_backend()
{
	return_result("hello", "world");
}
