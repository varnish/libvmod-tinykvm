#pragma once
#define SYSCALL_BASE 500

enum
{
	ECALL_FAIL = SYSCALL_BASE,
	ECALL_ASSERT_FAIL,
	ECALL_PRINT,
	ECALL_LOG,
	ECALL_BREAKPOINT,
	ECALL_SIGNAL,
	ECALL_DYNCALL,
	ECALL_REMOTECALL,
	ECALL_REMSTRCALL,

	ECALL_REGEX_COMPILE,
	ECALL_REGEX_MATCH,
	ECALL_REGEX_SUBST,
	ECALL_REGSUB_HDR,
	ECALL_REGEX_FREE,

	ECALL_MY_NAME,
	ECALL_SET_DECISION,
	ECALL_SET_BACKEND,
	ECALL_BACKEND_DECISION,
	ECALL_BAN,
	ECALL_HASH_DATA,
	ECALL_PURGE,
	ECALL_SYNTH,
	ECALL_CACHEABLE,
	ECALL_TTL,

	ECALL_FOREACH_FIELD,
	ECALL_FIELD_GET,
	ECALL_FIELD_RETRIEVE,
	ECALL_FIELD_APPEND,
	ECALL_FIELD_SET,
	ECALL_FIELD_COPY,
	ECALL_FIELD_UNSET,

	ECALL_HTTP_ROLLBACK,
	ECALL_HTTP_COPY,
	ECALL_HTTP_SET_STATUS,
	ECALL_HTTP_UNSET_RE,
	ECALL_HTTP_FIND,

	ECALL_SHA256,

	ECALL_LAST
};
