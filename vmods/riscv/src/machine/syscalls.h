#pragma once
#define SYSCALL_BASE 100

enum
{
	ECALL_SELF_TEST = SYSCALL_BASE,
	ECALL_ASSERT_FAIL,
	ECALL_PRINT,

	ECALL_REGEX_COMPILE,
	ECALL_REGEX_MATCH,
	ECALL_REGEX_SUBST,
	ECALL_REGEX_FREE,

	ECALL_FOREACH_FIELD,
	ECALL_FIELD_GET_L,
	ECALL_FIELD_GET,
	ECALL_FIELD_APPEND,
	ECALL_FIELD_SET,
	ECALL_FIELD_UNSET,

	ECALL_HTTP_SET_STATUS,

	ECALL_LAST
};
