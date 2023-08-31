#pragma once

#define KVM_PROGRAM_MAGIC 0x50ba93c7

#ifndef LIKELY
#define LIKELY(x) __builtin_expect((x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect((x), 0)
#endif
