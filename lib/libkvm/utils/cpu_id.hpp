#pragma once

namespace kvm
{
	static inline int cpu_id()
	{
		unsigned long a,d,c;
		asm volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
		return c & 0xFFF;
	}
}
