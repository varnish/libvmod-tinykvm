/**
 * @file vmod_compute.c
 * 
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-09-09
 * 
 * This file is the entry point for all compute VMOD interaction with VCL.
 * 
 */
#include "vmod_compute.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_if.h"

static const int NO_INIT_PROGRAMS = 0;

VCL_BOOL vmod_library(VRT_CTX, VCL_PRIV task, VCL_STRING uri)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	return (kvm_init_tenants_uri(ctx, task, uri, NO_INIT_PROGRAMS));
}

extern VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg);

VCL_BACKEND vmod_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING arg, VCL_STRING json_config)
{
	return (vmod_vm_backend(ctx, task, program, arg, json_config));
}
