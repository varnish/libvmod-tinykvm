/*-
 * Copyright (c) 2021 Varnish Software
 * All rights reserved.
 *
 * Author: Alve Elde <alve@varnish-software.com>
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

#ifndef _ADNS_H_INCLUDED_
#define _ADNS_H_INCLUDED_

#include <stdint.h>
#include <arpa/inet.h>
#include <vqueue.h>

#define ADNS_DEFAULT_TTL 10
#define ADNS_DEFAULT_PORT 80
#define ADNS_DEFAULT_IPV_RULE ADNS_IPV_AUTO
#define ADNS_DEFAULT_TTL_RULE ADNS_TTL_ABIDE
#define ADNS_DEFAULT_PORT_RULE ADNS_PORT_ABIDE
#define ADNS_DEFAULT_MODE_RULE ADNS_MODE_AUTO
#define ADNS_DEFAULT_UPDATE_RULE ADNS_UPDATE_ALWAYS
#define ADNS_DEFAULT_NSSWITCH_RULE ADNS_NSSWITCH_AUTO
#define ADNS_INFO_HASH_LEN 32

struct vcl;
enum vcl_event_e;

enum adns_ipv_rule {
	ADNS_IPV__NONE = 0,
	ADNS_IPV_AUTO,
	ADNS_IPV_IPV4,
	ADNS_IPV_IPV6,
	ADNS_IPV_ALL,

	ADNS_IPV__LAST
};

enum adns_ttl_rule {
	ADNS_TTL__NONE = 0,
	ADNS_TTL_FORCE,
	ADNS_TTL_ABIDE,
	ADNS_TTL_MORETHAN,
	ADNS_TTL_LESSTHAN,

	ADNS_TTL__LAST
};

enum adns_port_rule {
	ADNS_PORT__NONE = 0,
	ADNS_PORT_ABIDE,
	ADNS_PORT_FORCE,

	ADNS_PORT__LAST
};

enum adns_mode_rule {
	ADNS_MODE__NONE = 0,
	ADNS_MODE_AUTO,
	ADNS_MODE_HOST,
	ADNS_MODE_DNS,
	ADNS_MODE_SRV,

	ADNS_MODE__LAST
};

enum adns_update_rule {
	ADNS_UPDATE__NONE = 0,
	ADNS_UPDATE_ALWAYS,
	ADNS_UPDATE_IGNORE_EMPTY,
	ADNS_UPDATE_IGNORE_ERROR,

	ADNS_UPDATE__LAST
};

enum adns_nsswitch_rule {
	ADNS_NSSWITCH__NONE = 0,
	ADNS_NSSWITCH_AUTO,
	ADNS_NSSWITCH_DNS_ONLY,
	ADNS_NSSWITCH_DNS_FIRST,
	ADNS_NSSWITCH_FILES_ONLY,
	ADNS_NSSWITCH_FILES_FIRST,

	ADNS_NSSWITCH__LAST
};

enum adns_refresh_layer {
	ADNS_REFRESH__NONE = 0,
	ADNS_REFRESH_HOST,
	ADNS_REFRESH_CACHE,
	ADNS_REFRESH_INFO,

	ADNS_REFRESH__LAST
};

struct adns_rules {
	unsigned			magic;
#define ADNS_RULES_MAGIC		0x88A6A2F0
	enum adns_ipv_rule		ipv;
	enum adns_ttl_rule		ttl;
	enum adns_port_rule		port;
	enum adns_mode_rule		mode;
	enum adns_update_rule		update;
	enum adns_nsswitch_rule		nsswitch;
};

struct adns_hints {
	unsigned			magic;
#define ADNS_HINTS_MAGIC		0xB964C6ED
	const struct backend		*backend;
	const struct vrt_backend_probe	*probe;
	const char			*host;
	double				ttl;
	enum adns_refresh_layer		refresh_layer;
};

struct adns_info {
	unsigned			magic;
#define ADNS_INFO_MAGIC			0x33EF7D1E
	char				*name_a;
	char				*name_aaaa;
	struct suckaddr			*vsa4;
	struct suckaddr			*vsa6;
	VTAILQ_ENTRY(adns_info)		list;
	unsigned			touched;
	uint16_t			priority;
	uint16_t			weight;
	uint8_t				hash[ADNS_INFO_HASH_LEN];
};

VTAILQ_HEAD(adns_info_head, adns_info);

struct adns_info_list {
	unsigned			magic;
#define ADNS_INFO_LIST_MAGIC		0x3911F4BA
	struct adns_info_head		head;
	unsigned			len;
};

typedef void adns_update_cb_f(struct adns_info_list *new_list,
    struct adns_info_list *present, struct adns_info_list *removed,
    struct adns_hints *hints, void *priv);

void ADNS_event(struct vcl *vcl, enum vcl_event_e e);
int ADNS_config_default(struct vcl *vcl, const char *service, double ttl,
    struct adns_rules *rules, struct adns_hints *hints);
int ADNS_tag(const char *tag, struct vcl *vcl);
void ADNS_untag(const char *tag, struct vcl *vcl);
int ADNS_config(const char *tag, struct vcl *vcl, const char *host,
    const char *service, double ttl, struct adns_rules *rules,
    struct adns_hints *hints);
int ADNS_refresh(const char *tag, struct vcl *vcl,
    enum adns_refresh_layer layer, int wait);
int ADNS_subscribe(const char *tag, struct vcl *vcl,
    adns_update_cb_f *update_cb, void *priv);
void ADNS_unsubscribe(const char *tag, struct vcl *vcl,
    adns_update_cb_f *update_cb, void *priv);
const char *ADNS_err(int errorcode);

#endif	/* _ADNS_H_INCLUDED_ */
