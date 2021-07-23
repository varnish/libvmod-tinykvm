#include "vmod_kvm.h"
extern void init_tenants_str(VRT_CTX, const char *);
extern void init_tenants_file(VRT_CTX, const char *);

void vmod_embed_tenants(VRT_CTX, VCL_STRING json)
{
	if (json == NULL)
		return;

	init_tenants_str(ctx, json);
}
