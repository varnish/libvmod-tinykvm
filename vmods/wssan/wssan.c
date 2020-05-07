/*-
 * Copyright (c) 2011-2020 Varnish Software
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <cache/cache.h>
#include "vre.h"
#include "vsb.h"

#include <stdlib.h>
#include <string.h>

#include "vcc_if.h"

/*
 * vmod entrypoint.
 */
int
#ifdef VARNISH_PLUS
event_function
#else
vmod_event_function
#endif
	(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
	(void)ctx;
	(void)priv;

	if (e != VCL_EVENT_LOAD)
		return (0);
	/* loading bits */
	return (0);
}

static const char *cow =
	"\n ^__^\n"
	" (oo)\\_______\n"
	" (__)\\       )\\/\\\n"
	"      ||----w |\n"
	"      ||     ||\n";
static const char *bunny =
	"\n  (\\/)\n"
	"  (..)\n"
	" (\")(\")\n";

VCL_STRING
vmod_cowsay1(VRT_CTX, VCL_STRING animal, VCL_STRING talk)
{
	const char* ret = NULL;

	if (!strcmp(animal, "cow")) {
		ret = WS_Printf(ctx->ws, "** %s **\n%s", talk, cow);
	}
	else if (!strcmp(animal, "bunny")) {
		ret = WS_Printf(ctx->ws, "** %s **\n%s", talk, bunny);
	}

	/* WS_Printf can overflow the workspace and return NULL */
	return ((ret) ? ret : "");
}

VCL_STRING
vmod_cowsay2(VRT_CTX, VCL_STRING animal, VCL_STRING talk)
{
	const char* ret = NULL;
	//WS_VSB_new(vsb, ctx->ws);

	if (!strcmp(animal, "cow")) {
		ret = WS_Printf(ctx->ws, "** %s **\n%s", talk, cow);
	}
	else if (!strcmp(animal, "bunny")) {
		ret = WS_Printf(ctx->ws, "** %s **\n%s", talk, bunny);
	}

	/* WS_Printf can overflow the workspace and return NULL */
	return ((ret) ? ret : "");
}
