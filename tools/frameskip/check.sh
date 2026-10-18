#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
check_tmp=$(mktemp -d)
trap 'rm -rf "$check_tmp"' EXIT
cc -Wall -Wextra -fsanitize=address,undefined -g tools/frameskip/controller_test.c -o "$check_tmp/controller"
"$check_tmp/controller"
