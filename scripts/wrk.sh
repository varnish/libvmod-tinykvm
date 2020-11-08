#!/bin/bash
# 61277.72
wrk -c 20 -d 2 http://127.0.0.1:8080 | grep "Requests/sec" | awk -v N=2 '{print $N}'
