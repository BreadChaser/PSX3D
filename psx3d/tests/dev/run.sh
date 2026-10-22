#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/core/deps/libretro-common/include" -g "$ROOT/psx3d/tests/dev/check.c" -I"$ROOT/psx3d/source" "$ROOT/psx3d/source/clip.c" "$ROOT/psx3d/source/benchstate.c" "$ROOT/tools/input-replay/mock-core.c" -lz -o "$TMP/check"
(cd "$TMP" && ./check)

cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/core/deps/libretro-common/include" -I"$ROOT/psx3d/tests/dev" -g "$ROOT/psx3d/tests/dev/transport.c" $(pkg-config --cflags --libs libpng) -pthread -I"$ROOT/psx3d/source" "$ROOT/psx3d/source/clip.c" "$ROOT/psx3d/source/benchstate.c" "$ROOT/tools/input-replay/mock-core.c" -lz -lcrypto -o "$TMP/transport"
# The tracked client and benchmark driver also exercise the real TCP protocol.
HOST_TOOLS=${PSX3D_HOST_TOOLS:-$ROOT/tools/input-replay}
if [[ -f $HOST_TOOLS/psx3dctl.py && -f $HOST_TOOLS/fastbench.py ]]; then
  (cd "$TMP" && python3 "$ROOT/psx3d/tests/dev/client_test.py" "$TMP/transport")
else
  echo "SKIP client_test.py: psx3dctl.py and fastbench.py not found in $HOST_TOOLS (set PSX3D_HOST_TOOLS)"
  exit 77
fi
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/psx3d/tests/dev" -g "$ROOT/psx3d/tests/dev/update.c" -lcrypto -o "$TMP/update"
(cd "$TMP" && ./update)
