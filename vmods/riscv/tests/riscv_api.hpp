#include "../src/machine/syscalls.h"
#include <stddef.h>
#include <stdint.h>
#define  NOT_CACHED  0
#define  PASS        0
#define  CACHED      1

enum gethdr_e {
	HDR_REQ = 0,
	HDR_REQ_TOP,
	HDR_RESP,
	HDR_OBJ,
	HDR_BEREQ,
	HDR_BERESP
};

static inline long syscall1(long n, long a0);
static inline long syscall3(long n, long a0, long a1, long a2);
static inline long syscall4(long n, long a0, long a1, long a2, long a3);

inline void halt()
{
	asm volatile (".long 0x7ff00073" ::: "memory");
}

__attribute__((noreturn))
inline void forge_response(int status, const char* arg0, long arg1, const char* arg2, long arg3)
{
	register long a0 asm("a0") = status;
	register long a1 asm("a1") = (long) arg0;
	register long a2 asm("a2") = arg1;
	register long a3 asm("a3") = (long) arg2;
	register long a4 asm("a4") = arg3;

	asm volatile (".long 0x7ff00073"
		: "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory");
	__builtin_unreachable();
}

inline void decision(const char* dec, size_t declen, int status)
{
	register long a0 asm("a0") = (long) dec;
	register long a1 asm("a1") = declen;
	register long a2 asm("a2") = status;
	register long a3 asm("a3") = 0;
	register long syscall_id asm("a7") = ECALL_SET_DECISION;

	asm volatile ("ecall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(syscall_id) : "memory");
}

inline void forge(int c, void(*func)())
{
	register long a0 asm("a0") = c;
	register long a1 asm("a1") = (long) func;
	register long a2 asm("a2") = 0;
	register long syscall_id asm("a7") = ECALL_BACKEND_DECISION;

	asm volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(syscall_id) : "memory");
}

inline void synth(uint16_t status,
	const char* ctype, size_t clen, const char* data, size_t dlen)
{
	register long a0 asm("a0") = status;
	register long a1 asm("a1") = (long) ctype;
	register long a2 asm("a2") = clen;
	register long a3 asm("a3") = (long) data;
	register long a4 asm("a4") = dlen;
	register long syscall_id asm("a7") = ECALL_SYNTH;

	asm volatile ("ecall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(syscall_id) : "memory");
	__builtin_unreachable();
}

inline long header_field(int where, int idx, char* buffer, size_t maxlen)
{
	return syscall4(ECALL_FIELD_RETRIEVE, where, idx, (long)buffer, maxlen);
}

inline long get_url(int where, char* buffer, size_t maxlen)
{
	return header_field(where, 1, buffer, maxlen);
}

inline void wait_for_requests()
{
	register long a0 asm("a0") = (long)&"";
	register long a1 asm("a1") = 0;
	register long a2 asm("a2") = 100; // status
	register long a3 asm("a3") = 1; // 0=unpaused, 1=paused
	register long syscall_id asm("a7") = ECALL_SET_DECISION;

	asm volatile ("ecall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(syscall_id) : "memory");
}

extern "C" __attribute__((used, retain))
void on_recv(int req, int resp);
extern "C" __attribute__((used, retain))
void on_synth();
extern "C" __attribute__((used, retain))
void on_backend_fetch();

// 1. wrangle with argc and argc
// 2. initialize the global pointer to __global_pointer
// NOTE: have to disable relaxing first
asm
("   .global _start             \t\n\
_start:                         \t\n\
     lw   a0, 0(sp) 			\t\n\
	 addi a1, sp, 4		 		\t\n\
	 andi sp, sp, -16 /* not needed */\t\n\
     .option push 				\t\n\
	 .option norelax 			\t\n\
1:   auipc gp, %pcrel_hi(__global_pointer$) \t\n\
	 addi  gp, gp, %pcrel_lo(1b) \t\n\
	.option pop					\t\n\
	call start					\t\n\
");

#define DYNAMIC_CALL(isr, name, hash) \
	asm(".global " #name "\n" \
	#name ":\n" \
	"	li a7, " #isr "\n" \
	"	li t0, " #hash "\n" \
	"	ecall\n" \
	"   ret\n"); \
	extern "C" long name(...);
DYNAMIC_CALL(16, goto_dns, 0x746238D2)

extern "C" __attribute__((visibility("hidden"), used))
void start(int, char**);

// C library functions
static inline int strcmp(const char *s1, const char *s2)
{
    const uint8_t* p1 = (const uint8_t *)s1;
    const uint8_t* p2 = (const uint8_t *)s2;

    while (*p1 && *p1 == *p2) { ++p1; ++p2; }
    return (*p1 > *p2) - (*p2  > *p1);
}

// System call wrappers
long syscall1(long n, long arg0)
{
	register long a0 asm("a0") = arg0;
	register long syscall_id asm("a7") = n;

	asm volatile ("ecall" : "+r"(a0) : "r"(syscall_id));
	return a0;
}
long syscall3(long n, long arg0, long arg1, long arg2)
{
	register long a0 asm("a0") = arg0;
	register long a1 asm("a1") = arg1;
	register long a2 asm("a2") = arg2;
	register long syscall_id asm("a7") = n;

	asm volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(syscall_id));
	return a0;
}
long syscall4(long n, long arg0, long arg1, long arg2, long arg3)
{
	register long a0 asm("a0") = arg0;
	register long a1 asm("a1") = arg1;
	register long a2 asm("a2") = arg2;
	register long a3 asm("a3") = arg3;
	register long syscall_id asm("a7") = n;

	asm volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(syscall_id));
	return a0;
}
