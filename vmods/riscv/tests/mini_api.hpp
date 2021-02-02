#define  ECALL_BACKEND_DECISION  25
#define  NOT_CACHED  0
#define  CACHED      1

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

inline void forge(int c, void(*func)())
{
	register long a0 asm("a0") = c;
	register long a1 asm("a1") = (long) func;
	register long a2 asm("a2") = 0;
	register long syscall_id asm("a7") = ECALL_BACKEND_DECISION;

	asm volatile ("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(syscall_id) : "memory");
}

extern "C" __attribute__((used))
void on_recv();

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
