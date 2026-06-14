#!/usr/bin/env bash
set -euo pipefail
fail=0
while read -r want path token; do
    [ -z "${want:-}" ] && continue
    # Strip C/C++ comments and blank lines before hashing.
    got=$(sed -E 's@/\*.*\*/@@g; s@//.*@@; /^[[:space:]]*$/d' "$path" | sha256sum | awk '{print $1}')
    if [ "$got" != "$want" ]; then
        echo "DRIFT: $path changed since $token"
        echo "  expected $want"
        echo "  got      $got"
        echo "  action:  bump OEC_VERSION_{MAJOR,MINOR} in version.h AND update ci/known-good.txt"
        fail=1
    fi
done < ci/known-good.txt
exit $fail
