#!/usr/bin/env bash
echo "Sending $1"
curl --data-binary "@$1" -X POST http://localhost:8080 -D -
