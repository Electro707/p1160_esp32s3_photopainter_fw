#!/usr/bin/env bash
set -e

ESP_HOST="http://192.168.0.152"
FILE="$1"

curl -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @$FILE \
  $ESP_HOST/api/v1/fbRaw
