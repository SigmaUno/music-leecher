#!/usr/bin/env sh
set -eu

result=$(printf '%s\n' '{"request_id":"play-42","action":"resolve","song":{"title":"teardrop","artist":"massive attack"}}' | ./library-handler --library tests/library.fixture.json)
printf '%s' "$result" | grep -F '"request_id":"play-42"' >/dev/null
printf '%s' "$result" | grep -F '"found":true' >/dev/null
printf '%s' "$result" | grep -F '"PATH": "/music/' >/dev/null
printf '%s' "$result" | grep -F '"USERNAME": "music"' >/dev/null
printf '%s' "$result" | grep -F '"URL": "https://' >/dev/null
printf '%s' "$result" | grep -F '"IP": "192.0.2.20"' >/dev/null

missing=$(printf '%s\n' '{"action":"resolve","song":{"title":"not in library"}}' | ./library-handler --library tests/library.fixture.json)
printf '%s' "$missing" | grep -F '"found":false' >/dev/null
