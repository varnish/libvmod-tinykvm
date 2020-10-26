vcl 4.1;
import http;

backend default {
	.host = "localhost";
	.port = "8086";
}

sub vcl_recv {
	if (req.url ~ "(?i)authorise") {
		if (req.http.X-Token == "") {
			return(synth(200, "OK"));
		} else {
			// Initialize callout
			http.init(0);
			http.req_set_url(0, "http://127.0.0.1:8086/verify?token=" + req.http.X-Token);
			http.req_send(0);
			http.resp_wait(0);
			if (http.resp_get_status(0) != 200) {
				return(synth(200, "OK"));
			} else {
				return(synth(200, "Sending normal request to backend"));
			}
		}
	}
	/* Rest of vcl_recv */
}
