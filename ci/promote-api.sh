#!/usr/bin/env bash
set -euo pipefail
DIR=package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect
cat "$DIR/PublicAPI.Shipped.txt" "$DIR/PublicAPI.Unshipped.txt" \
    | sort -u > "$DIR/PublicAPI.Shipped.txt.new"
mv "$DIR/PublicAPI.Shipped.txt.new" "$DIR/PublicAPI.Shipped.txt"
: > "$DIR/PublicAPI.Unshipped.txt"
