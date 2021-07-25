#include "../tenant_instance.hpp"
#include "../varnish.hpp"

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

void initialize_vmod_goto(VRT_CTX)
{
	auto* vcl = ctx->vcl;
	assert (vcl != nullptr);

	struct vmod **goto_ptr = (struct vmod **)dlsym(vcl->dlh, "VGC_vmod_goto");
	struct vmod_priv *priv = (struct vmod_priv *)dlsym(vcl->dlh, "vmod_priv_goto");

	if (goto_ptr == nullptr || priv == nullptr) {
		printf("*** Goto dyncall: VMOD NOT FOUND\n");
		return;
	}

	struct vmod *vmod_goto = *goto_ptr;
	assert(vmod_goto != nullptr);

	auto* handle = VMOD_Handle(vmod_goto);
	if (handle == nullptr) return;

	vlookup(handle, td_goto_dns_director__init*, vmod_dns_director__init);
	vlookup(handle, td_goto_dns_director_backend*, vmod_dns_director_backend);

	const std::array<const char*, 3> ip_version {
		vmod_enum(handle, vmod_enum_all),
		vmod_enum(handle, vmod_enum_ipv4),
		vmod_enum(handle, vmod_enum_ipv6),
	};

	const std::array<const char*, 4> ttl_rule {
		vmod_enum(handle, vmod_enum_abide),
		vmod_enum(handle, vmod_enum_force),
		vmod_enum(handle, vmod_enum_morethan),
		vmod_enum(handle, vmod_enum_lessthan),
	};

	const std::array<const char*, 3> ignore_update {
		vmod_enum(handle, vmod_enum_onerror),
		vmod_enum(handle, vmod_enum_onempty),
		vmod_enum(handle, vmod_enum_never),
	};

	const std::array<const char*, 2> port_rule {
		vmod_enum(handle, vmod_enum_abide),
		vmod_enum(handle, vmod_enum_force),
	};

	TenantInstance::set_dynamic_call("goto.dns",
		[=] (MachineInstance& inst)
		{
			auto regs = inst.machine().registers();
			/* Hostname */
			std::string host;
			host.resize(regs.rsi);
			inst.machine().copy_from_guest(host.data(), regs.rdi, regs.rsi);
			/* IPV */
			const int ipv = regs.rdx;

			printf("Goto: %s (ipv=%s)\n", host.c_str(), ip_version.at(ipv));
			struct vmod_goto_dns_director *vo_d;
			vmod_dns_director__init(inst.ctx(), &vo_d, "d",
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
	          ip_version.at(ipv),
	          10,
	          ttl_rule.at(1),      // force
	          0,
	          ignore_update.at(2), // never
	          "",
	          port_rule.at(0)      // abide
	        );
			if (vo_d != nullptr) {
				const auto* dir = vmod_dns_director_backend(inst.ctx(), vo_d);
				regs.rax = inst.directors().manage(dir, 0);
			} else {
				printf("Failed to initialize DNS director\n");
				regs.rax = -1;
			}
			inst.machine().set_registers(regs);
		});
}

}
