#!/bin/bash
for i in {1..10}
do
    ab -q -k -c 5 -n 2500 http://localhost:8080/ | grep "Requests per" | awk -v N=4 '{print $N}'
done
