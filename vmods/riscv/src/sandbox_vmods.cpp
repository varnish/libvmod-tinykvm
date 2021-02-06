#include "sandbox.hpp"
#include "varnish.hpp"
#include <libriscv/util/crc32.hpp>
typedef struct oaref oaref_t;
extern "C" {
# include <vqueue.h>
# include <cache/cache_vcl.h>
}
#include <dlfcn.h>

extern "C" {
	struct vmod_goto_dns_director;
	typedef VCL_VOID td_goto_dns_director__init(VRT_CTX,
	    struct vmod_goto_dns_director **, const char *,
	    struct vmod_priv *, VCL_STRING, VCL_STRING, VCL_STRING,
	    VCL_DURATION, VCL_DURATION, VCL_DURATION, VCL_PROBE, VCL_INT,
	    VCL_BOOL, VCL_BOOL, VCL_BOOL, VCL_BOOL, VCL_ENUM, VCL_DURATION,
	    VCL_ENUM, VCL_ACL, VCL_ENUM, VCL_STRING, VCL_ENUM);
	typedef VCL_VOID td_goto_dns_director__fini(
	    struct vmod_goto_dns_director **);
	typedef VCL_BACKEND td_goto_dns_director_backend(VRT_CTX,
	    struct vmod_goto_dns_director *);

	/* Functions */
	typedef VCL_BACKEND td_goto_dns_backend(VRT_CTX,
	    struct vmod_priv *, struct vmod_priv *, VCL_STRING, VCL_STRING,
	    VCL_STRING, VCL_DURATION, VCL_DURATION, VCL_DURATION, VCL_PROBE,
	    VCL_INT, VCL_BOOL, VCL_BOOL, VCL_BOOL, VCL_BOOL, VCL_ENUM,
	    VCL_DURATION, VCL_ENUM, VCL_ACL, VCL_ENUM, VCL_STRING,
	    VCL_ENUM);

	void* VMOD_Handle(struct vmod *);
}

/*
extern VCL_ENUM vmod_enum_abide;
extern VCL_ENUM vmod_enum_all;
extern VCL_ENUM vmod_enum_force;
extern VCL_ENUM vmod_enum_ipv4;
extern VCL_ENUM vmod_enum_ipv6;
extern VCL_ENUM vmod_enum_lessthan;
extern VCL_ENUM vmod_enum_morethan;
extern VCL_ENUM vmod_enum_never;
extern VCL_ENUM vmod_enum_onempty;
extern VCL_ENUM vmod_enum_onerror;
*/

#define vlookup(handle, t, x) \
	auto x = (t) dlsym(handle, #x)

void SandboxTenant::init_vmods(VRT_CTX)
{
	auto* vcl = ctx->vcl;
	assert (vcl != nullptr);

	struct vmod **goto_ptr = (struct vmod **)dlsym(vcl->dlh, "VGC_vmod_goto");
	struct vmod_priv *priv = (struct vmod_priv *)dlsym(vcl->dlh, "vmod_priv_goto");

	if (goto_ptr == nullptr || priv == nullptr) {
		return;
	}

	struct vmod *vmod_goto = *goto_ptr;
	assert(vmod_goto != nullptr);

	auto* handle = VMOD_Handle(vmod_goto);
	if (handle == nullptr) return;

	vlookup(handle, td_goto_dns_director__init*, vmod_dns_director__init);
	vlookup(handle, td_goto_dns_director_backend*, vmod_dns_director_backend);
	vlookup(handle, VCL_ENUM, vmod_enum_all);
	vlookup(handle, VCL_ENUM, vmod_enum_force);
	vlookup(handle, VCL_ENUM, vmod_enum_never);
	vlookup(handle, VCL_ENUM, vmod_enum_abide);
	printf("*** Goto ENABLED\n");

	set_dynamic_call("goto.dns",
		[=] (auto& script)
		{
			auto [host] = script.machine().template sysargs<std::string> ();
			printf("Goto: %s\n", host.c_str());
			struct vmod_goto_dns_director *vo_d;
			vmod_dns_director__init(script.ctx(), &vo_d, "d",
	          priv,
	          host.c_str(), // host
	          "",           // port
	          "",           // Host: field
	          0,
	          0,
	          0,
	          0,
	          0,
	          0,
	          1,
	          1,
	          0,
	          vmod_enum_all,
	          10,
	          vmod_enum_force,
	          0,
	          vmod_enum_never,
	          "",
	          vmod_enum_abide
	        );
			if (vo_d != nullptr) {
				const auto* dir = vmod_dns_director_backend(script.ctx(), vo_d);
				int idx = script.directors().manage(dir, 0);
				script.machine().set_result(idx);
			} else {
				printf("Failed to initialize DNS director\n");
				script.machine().set_result(-1);
			}
		});
}
