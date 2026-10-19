#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
check_tmp=$(mktemp -d)
trap 'rm -rf "$check_tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  tools/frameskip-smart/check.c -lm -o "$check_tmp/controller"
"$check_tmp/controller"
