#!/usr/bin/env bash
set -euo pipefail
LABEL="external-breakage"
TITLE="external-breakage: $1 ($(date -u +%Y-W%V))"
# Ensure the label exists (a fresh repo won't have it; gh issue create aborts otherwise).
if ! gh label list --json name --jq '.[].name' | grep -qx "$LABEL"; then
    gh label create "$LABEL" --color B60205 \
        --description "Nightly canary: an upstream dependency broke the build" || true
fi
existing=$(gh issue list --label "$LABEL" --state open --search "$TITLE in:title" --json number --jq 'length')
if [ "$existing" = "0" ]; then
    gh issue create --label "$LABEL" --title "$TITLE" \
        --body "Nightly canary build failed against $1. See run: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID"
fi
