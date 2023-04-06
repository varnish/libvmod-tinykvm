cat << EOF >vclperf.vcl
vcl 4.1;
import file;
import utils;

backend default {
	.host = "127.0.0.1";
	.port = "8000";
}

sub vcl_init {
	new f = file.init("/tmp");
}

EOF

for N in {1..1000}
do
    cat << EOF >>vclperf.vcl
acl local$N {
    "localhost";
    "192.168.1.0"/24;
    ! "192.168.1.23";
}
EOF
done

cat << EOF >>vclperf.vcl
sub vcl_recv {
EOF

for N in {1..1000}
do
    cat << EOF >>vclperf.vcl
if (client.ip ~ local$N) {
    return (hash);
}
EOF
done

cat << EOF >>vclperf.vcl
	set req.backend_hint = f.backend();
	return (pass);
}

EOF
