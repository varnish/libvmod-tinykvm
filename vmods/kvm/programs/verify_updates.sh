#!/usr/bin/env bash
echo "Verifying live updates using tenant $1"

for i in {1..1000}
do
	curl -H "Host: $1" --data-binary "@/tmp/xpizza" -X POST http://localhost:8080
	curl -H "Host: $1" --data-binary "@/tmp/ypizza" -X POST http://localhost:8080
done
