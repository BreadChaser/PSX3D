#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -g "$ROOT/psx3d/tests/dev/check.c" -o "$TMP/check"
(cd "$TMP" && ./check)

cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -g "$ROOT/psx3d/tests/dev/transport.c" $(pkg-config --cflags --libs libpng) -pthread -o "$TMP/transport"
python3 "$ROOT/psx3d/tests/dev/client_test.py" "$TMP/transport"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/psx3d/tests/dev" -g "$ROOT/psx3d/tests/dev/update.c" -lcrypto -o "$TMP/update"
(cd "$TMP" && ./update)
