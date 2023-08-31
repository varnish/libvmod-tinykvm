#!/usr/bin/env bash
echo "Sending $2 to $1"
curl -H "Host: $1" --data-binary "@$2" -X POST http://localhost:8080
