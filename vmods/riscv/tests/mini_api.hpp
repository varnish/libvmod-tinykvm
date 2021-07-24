#include "../src/machine/syscalls.h"
#include <stddef.h>
#include <stdint.h>
#define  NOT_CACHED  0
#define  PASS        0
#define  CACHED      1

inline long syscall(long n, long arg0)
{
	register long a0 asm("a0") = arg0;
	register long syscall_id asm("a7") = n;

	asm volatile ("scall" : "+r"(a0) : "r"(syscall_id));

	return a0;
}
inline long syscall(long n, long arg0, long arg1, long arg2)
{
	register long a0 asm("a0") = arg0;
	register long a1 asm("a1") = arg1;
	register long a2 asm("a2") = arg2;
	register long syscall_id asm("a7") = n;

	asm volatile ("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(syscall_id));

	return a0;
}

extern "C" __attribute__((noreturn))
void exit(int code)
{
	register long a0 asm("a0") = code;

	asm volatile (".long 0x7ff00073" :: "r"(a0));
	__builtin_unreachable();
}

__attribute__((noreturn))
inline void forge_response(const char* arg0, long arg1, const char* arg2, long arg3)
{
	register long a0 asm("a0") = (long) arg0;
	register long a1 asm("a1") = arg1;
	register long a2 asm("a2") = (long) arg2;
	register long a3 asm("a3") = arg3;

	asm volatile (".long 0x7ff00073"
		: "+r"(a0) : "r"(a1), "r"(a2), "r"(a3) : "memory");
	__builtin_unreachable();
}

inline void decision(const char* dec, size_t declen, int status)
{
	register long a0 asm("a0") = (long) dec;
	register long a1 asm("a1") = declen;
	register long a2 asm("a2") = status;
	register long a3 asm("a3") = 0;
	register long syscall_id asm("a7") = ECALL_SET_DECISION;

	asm volatile ("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(syscall_id) : "memory");
}

inline void forge(int c, void(*func)())
{
	register long a0 asm("a0") = c;
	register long a1 asm("a1") = (long) func;
	register long a2 asm("a2") = 0;
	register long syscall_id asm("a7") = ECALL_BACKEND_DECISION;

	asm volatile ("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(syscall_id) : "memory");
}

inline void synth(const char* ctype, size_t clen, const char* data, size_t dlen)
{
	register long a0 asm("a0") = (long) ctype;
	register long a1 asm("a1") = clen;
	register long a2 asm("a2") = (long) data;
	register long a3 asm("a3") = dlen;
	register long syscall_id asm("a7") = ECALL_SYNTH;

	asm volatile ("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(syscall_id) : "memory");
	__builtin_unreachable();
}

extern "C" __attribute__((used))
void on_recv();
extern "C" __attribute__((used))
void on_synth();
extern "C" __attribute__((used))
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
