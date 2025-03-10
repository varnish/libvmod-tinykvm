set -e
varnishd -f $PWD/tinykvm.vcl -a :8080 -a /tmp/tinykvm.sock -n /tmp/varnishd -F
