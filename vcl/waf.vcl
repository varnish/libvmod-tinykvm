#waf.vcl
#
# To use this vcl, simply include it at the top of your vcl, all
# necessary subroutines will be automatically called. Options can
# be set with the kvstore waf_opts.
#

import std;
import kvstore;
import waf;

#
# options will be set in vcl_init with waf_opts. For example:
#
# 	sub vcl_init {
#		waf_opts.("cache_req_body_bytes", "10MB")
#	}
#
# Options are all strings. The options are as follows:
#
# cache_req_body_bytes: The max size of request body varnish will
# store set in vcl_recv. Defaults to 1MB
#
# Headers can change three things: client ip/port and whether to
# Skip ModSecurity for the request or response.
#
# waf-client-ip: IP address for client. Defaults to  first XFF or client.ip.
# waf-client-port: port address for client. Defaults to 0 or std.port(client.ip).
# waf-skip: choose to skip ModSecurity all together ("true"), only request
# parsing ("request") or only response parsing (response). Defaults to null
#

sub vcl_init {
	new waf_opts = kvstore.init();
	new varnish_waf = waf.init();
}

sub vcl_recv {
	std.cache_req_body(waf.bytes(waf_opts.get("cache_req_body_bytes"), 1MB));
	# get first XFF ip if avail otherwise take client ip/port
	if (req.http.X-Forwarded-For) {
		set req.http.waf-client-ip = regsub(req.http.X-Forwarded-For, ",.*$", "");
		unset req.http.waf-client-port;
	} else {
		set req.http.waf-client-ip = client.ip;
		set req.http.waf-client-port = std.port(client.ip);
	}
}

sub vcl_backend_fetch {
	# Initialize transaction data
	varnish_waf.init_transaction();
	# Finish setting up transaction data
	# varnish_waf.setup_transaction();
	# check req.body/headers. if there is an
	# disruptive action, go to v_b_e
	if (varnish_waf.check_req(bereq.http.waf-client-ip,
		std.integer(bereq.http.waf-client-port, 0))) {
		return (error);
	}
}

sub waf_check_fini {
	# send info to audit log
	varnish_waf.audit_log();
	# if there is an interruption, get the status, possible
	# redirect and send back an empty body
	if (varnish_waf.disruptive()) {
		set beresp.status = varnish_waf.disrupt_status();
		if (varnish_waf.disrupt_url()) {
			set beresp.http.location = varnish_waf.disrupt_url();
		}
		set beresp.http.connection = "close";
		set beresp.http.content-length = "0";
		set beresp.uncacheable = true;
		set beresp.ttl = 0s;
		set beresp.grace = 0s;
		return (deliver);
	}
}

sub vcl_backend_response {
	# check resp.body/headers
	varnish_waf.check_resp();
	call waf_check_fini;
}

sub vcl_backend_error {
	call waf_check_fini;
}

#EOF
