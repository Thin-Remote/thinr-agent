#!/usr/bin/env bash
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# Generate a channel manifest (<channel>.json) for thinr-agent, including the
# staged-rollout fields consumed by the agent >= 1.8.0.
#
# This writes a JSON file; it never touches AWS. It is the testable core of the
# manifest step in build.yml and can be run locally against a directory of
# (possibly fake) binaries.
#
# Manifest contract (shared with the staged-rollout agent task #2):
#   version       string  exact tag/channel string ("v1.8.0", "main", ...)
#   checksums     object  { "<arch-suffix>": "sha256:<64 hex>" }  one per binary
#   published_at  integer epoch seconds UTC, taken once per run
#   soak_h        integer hours of quiet period before the ramp starts
#   ramp_h        integer hours over which exposure widens
#   frozen_at     (never emitted here; publication is always unfrozen)
#
# Branch channels (main/develop) MUST pass --soak-h 0 --ramp-h 0 so they impose
# no soak/ramp on the current updater.
#
# Usage:
#   gen-manifest.sh --binaries-dir DIR --version VER --out FILE
#                   [--soak-h N] [--ramp-h N] [--published-at EPOCH]
#   gen-manifest.sh --check-params --soak-h N --ramp-h N
#
# Defaults: --soak-h 72 --ramp-h 96 (the stable-release defaults, FR-002),
#           --published-at = now.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "${SCRIPT_DIR}/lib.sh"

BINARIES_DIR=""
VERSION=""
OUT=""
SOAK_H="72"
RAMP_H="96"
PUBLISHED_AT=""
CHECK_PARAMS=0

usage() {
    sed -n '20,33p' "$0"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --binaries-dir) BINARIES_DIR="$2"; shift 2 ;;
        --version)      VERSION="$2";      shift 2 ;;
        --out)          OUT="$2";          shift 2 ;;
        --soak-h)       SOAK_H="$2";       shift 2 ;;
        --ramp-h)       RAMP_H="$2";       shift 2 ;;
        --published-at) PUBLISHED_AT="$2"; shift 2 ;;
        --check-params) CHECK_PARAMS=1;    shift ;;
        -h|--help)      usage 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage 1 ;;
    esac
done

# Validate ramp parameters first: must fail before anything is written/uploaded.
thinr_validate_uint "soak_h" "$SOAK_H"
thinr_validate_uint "ramp_h" "$RAMP_H"

if [ "$CHECK_PARAMS" -eq 1 ]; then
    echo "ok: soak_h=${SOAK_H} ramp_h=${RAMP_H} are valid non-negative integers"
    exit 0
fi

[ -n "$BINARIES_DIR" ] || { echo "error: --binaries-dir is required" >&2; exit 1; }
[ -n "$VERSION" ]      || { echo "error: --version is required" >&2; exit 1; }
[ -n "$OUT" ]          || { echo "error: --out is required" >&2; exit 1; }
[ -d "$BINARIES_DIR" ] || { echo "error: binaries dir not found: ${BINARIES_DIR}" >&2; exit 1; }

if [ -z "$PUBLISHED_AT" ]; then
    PUBLISHED_AT="$(date -u +%s)"
fi
thinr_validate_uint "published_at" "$PUBLISHED_AT"

CHECKSUMS="$(thinr_checksums_json "$BINARIES_DIR")"

jq -n \
    --arg     version      "$VERSION" \
    --argjson checksums    "$CHECKSUMS" \
    --argjson published_at "$(thinr_norm_uint "$PUBLISHED_AT")" \
    --argjson soak_h       "$(thinr_norm_uint "$SOAK_H")" \
    --argjson ramp_h       "$(thinr_norm_uint "$RAMP_H")" \
    '{version: $version, checksums: $checksums, published_at: $published_at, soak_h: $soak_h, ramp_h: $ramp_h}' \
    > "$OUT"

echo "wrote ${OUT}: version=${VERSION} published_at=${PUBLISHED_AT} soak_h=${SOAK_H} ramp_h=${RAMP_H}"
