#!/usr/bin/env bash
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# Freeze / unfreeze the in-progress staged rollout by setting or removing
# `frozen_at` in latest.json, without touching version, checksums, binaries or
# any other field. Idempotent in both directions. Invalidates /latest.json.
#
# `frozen_at` is an integer epoch-seconds-UTC timestamp. Its absence means the
# ramp is not frozen (shared contract with task #2).
#
# Modes (pick one):
#   --cdn-dir DIR     local simulation of the CDN root (no AWS)
#   --bucket NAME     real S3 bucket (CI); add --cloudfront-id ID to invalidate
#
# Usage:
#   freeze.sh (--freeze | --unfreeze) (--cdn-dir DIR | --bucket NAME [--cloudfront-id ID])
#             [--dry-run]

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "${SCRIPT_DIR}/lib.sh"

ACTION=""
export THINR_CDN_MODE=""
export THINR_CDN_DIR=""
export THINR_S3_BUCKET=""
export THINR_CLOUDFRONT_ID=""
export THINR_DRY_RUN=0

usage() { sed -n '16,20p' "$0"; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --freeze)        ACTION="freeze"; shift ;;
        --unfreeze)      ACTION="unfreeze"; shift ;;
        --cdn-dir)       THINR_CDN_MODE="local"; THINR_CDN_DIR="$2"; shift 2 ;;
        --bucket)        THINR_CDN_MODE="s3"; THINR_S3_BUCKET="$2"; shift 2 ;;
        --cloudfront-id) THINR_CLOUDFRONT_ID="$2"; shift 2 ;;
        --dry-run)       THINR_DRY_RUN=1; shift ;;
        -h|--help)       usage 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage 1 ;;
    esac
done

fail() { echo "freeze: error: $*" >&2; echo "freeze: aborted, nothing was modified" >&2; exit 1; }

[ -n "$ACTION" ] || fail "pick an action: --freeze or --unfreeze"
[ -n "$THINR_CDN_MODE" ] || fail "pick a mode: --cdn-dir DIR or --bucket NAME"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

thinr_cdn_fetch "latest.json" "${WORK}/latest.json" || fail "could not read current latest.json"

HAS_FROZEN="$(jq 'has("frozen_at")' "${WORK}/latest.json")"
VERSION="$(jq -r '.version' "${WORK}/latest.json")"

if [ "$ACTION" = "freeze" ]; then
    if [ "$HAS_FROZEN" = "true" ]; then
        EXISTING="$(jq -r '.frozen_at' "${WORK}/latest.json")"
        echo "freeze: latest (${VERSION}) is already frozen at ${EXISTING}; no change (idempotent)"
        exit 0
    fi
    FROZEN_AT="$(date -u +%s)"
    jq --argjson frozen_at "$FROZEN_AT" '. + {frozen_at: $frozen_at}' \
        "${WORK}/latest.json" > "${WORK}/new-latest.json"
    echo "freeze: freezing latest (${VERSION}) ramp at frozen_at=${FROZEN_AT}"
else
    if [ "$HAS_FROZEN" != "true" ]; then
        echo "freeze: latest (${VERSION}) is not frozen; no change (idempotent)"
        exit 0
    fi
    jq 'del(.frozen_at)' "${WORK}/latest.json" > "${WORK}/new-latest.json"
    echo "freeze: unfreezing latest (${VERSION}) ramp"
fi

thinr_cdn_put_file "${WORK}/new-latest.json" "latest.json" "application/json" "max-age=60"
thinr_cdn_invalidate "/latest.json"

echo "freeze: done (${ACTION}) on ${VERSION} at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
