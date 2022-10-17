#pragma once
#include <stdint.h>

#include "../../kvm/src/vmod_kvm.h"

extern int insular_initial_program(VRT_CTX, VCL_PRIV, const char *uri);
extern int insular_execute(VRT_CTX, int entry, const char *arg);
