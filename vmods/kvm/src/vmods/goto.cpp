#include "../tenant.hpp"
#include "../machine_instance.hpp"
#include "../varnish.hpp"
#include <cassert>
#include <stdexcept>

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
	typedef struct vmod *(*vmod_handle_f) ();
	typedef struct vmod_priv *(*vmod_priv_handle_f) ();
}

namespace kvm {

#define vlookup(handle, t, x) \
	auto x = (t) dlsym(handle, #x)
inline auto* validate_deref(const char** ptr, const char* s) {
	if (ptr != nullptr)
		return *ptr;
	throw std::runtime_error("Invalid pointer for enum: " + std::string(s));
}
#define vmod_enum(handle, x) \
	validate_deref((const char **)dlsym(handle, #x), #x)

void initialize_vmod_goto(VRT_CTX, VCL_PRIV task)
{
	auto* vcl = ctx->vcl;
	assert (vcl != nullptr);

	/* Discover vmod_goto. */
	struct vmod *vmod_goto = /*VMOD_ForEach(
		[] (struct vmod *vmod) -> struct vmod * {
			auto* dlh = VMOD_Handle(vmod);
			if (dlsym(dlh, "vmod_dns_director__init"))
				return vmod;
			return nullptr;
		});*/
		nullptr;

	if (vmod_goto == nullptr) {
		printf("*** Goto dyncall: VMOD NOT FOUND\n");
		TenantConfig::reset_dynamic_call(task, "goto.dns");
		return;
	}
}

} // kvm
