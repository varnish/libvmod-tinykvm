set solib-search-path /tmp/varnishd:/tmp/varnishd/vmod_cache

define lsof
    shell rm -f pidfile
    set logging file pidfile
    set logging on
    info proc
    set logging off
    shell lsof -p `cat pidfile | perl -n -e 'print $1 if /process (.+)/'`
end

define varnish
    run -a :8080 -b :8081 -F -n /tmp/varnishd
end
