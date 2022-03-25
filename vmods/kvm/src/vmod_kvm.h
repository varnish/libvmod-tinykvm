#pragma once
#include <stdint.h>

#include "vtree.h"

#include "cache/cache_varnishd.h"
#include "cache/cache_director.h"
#include "cache/cache_filter.h"
typedef struct vmod_priv * VCL_PRIV;

#define KVM_BACKEND_MAGIC 0x645603935c082fa6
#define KVM_UPDATER_MAGIC 0xb39b941de14e9f89

#define KVM_FORK_MAIN      0
#define KVM_FORK_DEBUG     1


struct vmod_kvm_backend
{
	uint64_t magic;

	uint64_t max_time;

	struct director dir;
};

struct vmod_kvm_updater
{
	uint64_t magic;
	struct director dir;

	uint64_t max_binary_size;
	struct vmod_kvm_tenant *tenant;
	int16_t  is_debug;
	uint16_t debug_port;
};

struct vmod_kvm_response
{
	uint64_t magic;
	struct director dir;

	const void *priv_key;
	struct vmod_kvm_tenant *tenant;
	const char *funcarg;
	uint64_t max_response_size;
	int16_t is_post;
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

extern void kvm_init_tenants_str(VRT_CTX, VCL_PRIV, const char *);
extern void kvm_init_tenants_file(VRT_CTX, VCL_PRIV, const char *);
extern void initialize_vmods(VRT_CTX, VCL_PRIV);
extern TEN_PTR kvm_tenant_find(VCL_PRIV, const char *name);
extern TEN_PTR kvm_tenant_find_key(VCL_PRIV, const char *name, const char *key);
extern int     kvm_tenant_gucci(TEN_PTR, int debug);
extern KVM_SLOT kvm_reserve_machine(VRT_CTX, TEN_PTR, int);
extern int kvm_call(VRT_CTX, KVM_SLOT, const char *func, const char *arg);
extern int kvm_callv(VRT_CTX, KVM_SLOT, const int, const char *arg);
extern int kvm_synth(VRT_CTX, KVM_SLOT, struct vmod_kvm_synth *);
extern uint64_t kvm_resolve_name(TEN_PTR, const char*);
extern int kvm_copy_to_machine(KVM_SLOT, uint64_t dst, const void* src, size_t len);

static inline void kvm_ts(struct vsl_log *vsl, const char *event,
		double *work, double *prev, double now)
{
	VSLb_ts(vsl, event, *work, prev, now);
	*work = now;
}
