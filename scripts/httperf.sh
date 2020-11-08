#!/bin/bash
# ./varnishd  -a :8080 -f $PWD/../esi.vcl -F -p cc_command="exec gcc -g -O2 -Wall -Werror -Wno-error=unused-result -pthread -fpic -shared -Wl,-x -o %o %s"

for i in {1..60}
do
    ab -q -k -c 5 -n 1000000 http://localhost:8080/ | grep "Requests per" | awk -v N=4 '{print $N}'
done
