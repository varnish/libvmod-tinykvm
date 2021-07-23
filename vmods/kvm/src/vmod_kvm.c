#include "vmod_kvm.h"
extern void kvm_init_tenants_str(VRT_CTX, const char *);
extern void kvm_init_tenants_file(VRT_CTX, const char *);

void vmod_embed_tenants(VRT_CTX, VCL_STRING json)
{
	if (json == NULL)
		return;

	kvm_init_tenants_str(ctx, json);
}
