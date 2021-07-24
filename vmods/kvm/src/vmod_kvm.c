#include "vmod_kvm.h"
extern void kvm_init_tenants_str(VRT_CTX, const char *);
extern void kvm_init_tenants_file(VRT_CTX, const char *);

void vmod_embed_tenants(VRT_CTX, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx, "kvm.embed_tenants() requires a JSON string");
		return;
	}

	kvm_init_tenants_str(ctx, json);
}

VCL_VOID vmod_load_tenants(VRT_CTX, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (filename == NULL || filename[0] == 0) {
		VRT_fail(ctx, "kvm.load_tenants() requires a filename");
		return;
	}

	kvm_init_tenants_file(ctx, filename);
}
