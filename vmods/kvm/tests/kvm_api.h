#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

asm(".global backend_response\n" \
"backend_response:\n" \
"	mov $0xFFFF, %eax\n" \
"	out %eax, $0\n" \
"   ret\n"); \
extern void __attribute__((noreturn))
backend_response(const void *t, uint64_t, const void *c, uint64_t);

void return_result(const char *ctype, const char *content)
{
	backend_response(ctype, strlen(ctype), content, strlen(content));
}

#define DYNAMIC_CALL(name, hash) \
	asm(".global " #name "\n" \
	#name ":\n" \
	"	mov " #hash ", %eax\n" \
	"	out %eax, $1\n" \
	"   ret\n"); \
	extern long name();
DYNAMIC_CALL(goto_dns, 0x746238D2)

#ifdef __cplusplus
}
#endif
