#!/usr/bin/env sh
set -eu

temporary_library=$(mktemp)
trap 'rm -f "$temporary_library"' EXIT
cp tests/library.fixture.json "$temporary_library"
./test-library-mutation "$temporary_library"
