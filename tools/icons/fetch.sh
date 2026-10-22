#!/bin/sh
# Download the Dreamstale originals (512 px PNG) that icons.tsv uses into DIR
# (outside the repo: only the packed masks ship, never the originals).
#   tools/icons/fetch.sh DIR
set -e
dir=${1:?usage: fetch.sh DIR}
mkdir -p "$dir"
grep -v '^#' "$(dirname "$0")/icons.tsv" | cut -f3 | sort -u | while read -r id; do
	[ -s "$dir/$id.png" ] && continue
	curl -fsSL -o "$dir/$id.png" "https://cdn-icons-png.flaticon.com/512/${id%???}/$id.png"
done
