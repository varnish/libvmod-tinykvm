#pragma once
#include <stdint.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_director.h"
#include "cache/cache_filter.h"
typedef struct vmod_priv * VCL_PRIV;

#define KVM_BACKEND_MAGIC 0x645603935c082fa6
#define KVM_UPDATER_MAGIC 0xb39b941de14e9f89

#define KVM_FORK_MAIN      0
#define KVM_FORK_DEBUG     1


struct vmod_kvm_updater
{
	uint64_t magic;
	struct director dir;

	uint64_t max_binary_size;
	struct vmod_kvm_tenant *tenant;
	int16_t  is_debug;
	uint16_t debug_port;
};

struct vmod_kvm_synth
{
	struct vsb *vsb;
	uint16_t status;
	char ct_buf[242];
	int  ct_len;
};

typedef struct vmod_kvm_tenant * TEN_PTR;
typedef struct vmod_kvm_slot * KVM_SLOT;
typedef int (*vmod_kvm_foreach_t) (const char *, TEN_PTR, void *);

extern int  kvm_init_tenants_str(VRT_CTX, VCL_PRIV, const char *source, const char *, size_t, int init);
extern int  kvm_init_tenants_file(VRT_CTX, VCL_PRIV, const char *, int init);
extern int  kvm_init_tenants_uri(VRT_CTX, VCL_PRIV, const char *uri, int init);
extern const char * kvm_tenant_name(TEN_PTR tenant);
extern const char * kvm_json_stats(VRT_CTX, VCL_PRIV task, const char *pattern, unsigned indent);
extern TEN_PTR kvm_tenant_find(VCL_PRIV, const char *name);
extern TEN_PTR kvm_tenant_find_key(VCL_PRIV, const char *name, const char *key);
extern int     kvm_tenant_foreach(VCL_PRIV, vmod_kvm_foreach_t func, void *);
extern int     kvm_tenant_debug_allowed(TEN_PTR);
extern int     kvm_tenant_gucci(TEN_PTR, int debug);
extern int     kvm_tenant_configure(VRT_CTX, TEN_PTR, const char *json);
extern int     kvm_tenant_arguments(VRT_CTX, TEN_PTR, size_t n, const char **strings);
extern int     kvm_tenant_async_start(VRT_CTX, TEN_PTR, int debug);
extern int     kvm_tenant_unload(VRT_CTX, TEN_PTR);
extern KVM_SLOT kvm_reserve_machine(VRT_CTX, TEN_PTR, int debug);
extern KVM_SLOT kvm_temporarily_reserve_machine(VRT_CTX, TEN_PTR, int debug);
extern void     kvm_free_reserved_machine(VRT_CTX, void* slot);
#ifdef VARNISH_PLUS
extern vmod_priv_free_f *kvm_get_free_function();
#else
extern vmod_priv_fini_f *kvm_get_free_function();
#endif
extern int kvm_callv(VRT_CTX, KVM_SLOT, const int, const char *arg);
extern int kvm_synth(VRT_CTX, KVM_SLOT, struct vmod_kvm_synth *);
extern uint64_t kvm_resolve_name(TEN_PTR, const char*);
extern int kvm_copy_to_machine(KVM_SLOT, uint64_t dst, const void* src, size_t len);
extern uint64_t kvm_allocate_memory(KVM_SLOT, uint64_t bytes);
extern int      kvm_is_mmap_range(KVM_SLOT, uint64_t addr);

struct backend_result;
extern int kvm_set_self_request(VRT_CTX, VCL_PRIV, const char *unix_path, const char *uri, long max);
extern int kvm_self_request(VRT_CTX, const char *path, const char *config, struct backend_result *result);

/* Fetch something with cURL. Returns 0: success, <0: failure */
struct MemoryStruct
{
	char *memory;
	size_t size;
};
extern int
kvm_curl_fetch(VRT_CTX, const char *url, void(*callback)(void*, struct MemoryStruct *), void *usr);

/* Varnish statistics */
extern struct VSC_vmod_kvm *vsc_vmod_kvm;
