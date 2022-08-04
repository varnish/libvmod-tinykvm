extern "C" {
#include "vdef.h"
#include "vrt.h"
#include "vcl.h"
#include "vas.h"
#include "miniobj.h"
#include <adns/adns.h>
}

extern "C"
void handle_vmod_event(struct vcl *vcl, enum vcl_event_e e)
{
    ADNS_event(vcl, e);
}
