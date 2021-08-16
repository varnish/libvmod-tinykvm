#pragma once

#include <sys/syscall.h>
extern "C" long syscall(long);

#define gettid() ((pid_t)syscall(SYS_gettid))
