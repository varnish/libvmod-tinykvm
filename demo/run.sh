set -e
VARNISHD=varnishd
#VARNISHD=~/github/varnish_autoperf/build/varnishd

$VARNISHD -f $PWD/tinykvm.vcl -a :8080 -a /tmp/tinykvm.sock -n /tmp/varnishd -F
