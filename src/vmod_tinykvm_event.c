#include "config.h"

#include "vdef.h"
#include "vrt.h"
#include "vcl.h"
#include "vas.h"
#include "miniobj.h"

#include "vcc_if.h"
#include "VSC_vmod_kvm.h"

extern void handle_vmod_event(struct vcl *vcl, enum vcl_event_e e);
#ifdef VARNISH_PLUS
#define EVENT_NAME  vmod_event
#else
#define EVENT_NAME  vmod_vmod_event /* ??? */
#endif

static unsigned vsc_init_counter = 0;
static struct   vsc_seg *vsc_segment;
struct VSC_vmod_kvm *vsc_vmod_kvm;

static void vsc_init()
{
	if (vsc_init_counter++ == 0) {
		vsc_vmod_kvm = VSC_vmod_kvm_New(NULL, &vsc_segment, "");
	}
}
static void vsc_fini()
{
	if (--vsc_init_counter > 0)
		return;
	VSC_vmod_kvm_Destroy(&vsc_segment);

	vsc_vmod_kvm = NULL;
}

int v_matchproto_(vmod_event_f)
	EVENT_NAME(VRT_CTX, struct vmod_priv *vp, enum vcl_event_e e)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void) vp;

#ifdef KVM_ADNS
	handle_vmod_event(ctx->vcl, e);
#endif

	switch (e) {
	case VCL_EVENT_LOAD:
		vsc_init();
		break;

	case VCL_EVENT_DISCARD:
		vsc_fini();
		break;

	default:
		break;
	}

	// NOTE: Tenancy structure is automatically freed.
	return (0);
}

void kvm_varnishstat_program_cpu_time(vtim_real duration, vtim_real fetch_duration)
{
	static const uint64_t precision = 1024 * 1024UL; /* Arbritrary, good enough. */
	static uint64_t cpu_time_kinda;
	static uint64_t fetch_time_kinda;

	__sync_fetch_and_add(&cpu_time_kinda, duration * precision);
	vsc_vmod_kvm->cpu_time = cpu_time_kinda / precision;

	if (fetch_duration > 0.0) {
		__sync_fetch_and_add(&fetch_time_kinda, fetch_duration * precision);
		vsc_vmod_kvm->fetch_time = fetch_time_kinda / precision;
	}
}

void kvm_varnishstat_program_exception()
{
	__sync_fetch_and_add(&vsc_vmod_kvm->program_exception, 1);
}

void kvm_varnishstat_program_timeout()
{
	__sync_fetch_and_add(&vsc_vmod_kvm->program_timeout, 1);
}

void kvm_varnishstat_self_request(int failed)
{
	__sync_fetch_and_add(&vsc_vmod_kvm->self_requests, 1);
	if (failed)
		__sync_fetch_and_add(&vsc_vmod_kvm->self_requests_failed, 1);
}

void kvm_varnishstat_program_status(uint16_t status)
{
	if (status >= 500)
		__sync_fetch_and_add(&vsc_vmod_kvm->program_status_5xx, 1);
	else if (status >= 400)
		__sync_fetch_and_add(&vsc_vmod_kvm->program_status_4xx, 1);
}
