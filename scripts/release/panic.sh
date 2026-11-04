#!/usr/bin/env bash
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# Panic button: re-point the stable `latest` channel at a previously archived
# stable release, so the whole auto-update fleet moves to it (the agent applies
# on current != latest, so already-updated devices DOWNGRADE, through the
# rollback watchdog).
#
# It validates everything BEFORE mutating anything: the target must be a stable
# version, every architecture currently served by latest must have an archived
# binary, and each SHA256 must be computable. On any failure nothing is written.
# Binaries are uploaded first, the manifest last. It never uses --delete and
# never touches versioned binaries.
#
# Modes (pick one):
#   --cdn-dir DIR     local simulation of the CDN root (no AWS); reads/writes
#                     DIR/latest.json, DIR/binaries/latest/, DIR/binaries/<v>/
#   --bucket NAME     real S3 bucket (CI); add --cloudfront-id ID to invalidate
#
# Usage:
#   panic.sh --version vN.N.N (--cdn-dir DIR | --bucket NAME [--cloudfront-id ID])
#            [--actor NAME] [--dry-run]
#
# The target manifest is published with soak_h=0, ramp_h=0 and no frozen_at, and
# published_at pinned in the far past (1): every device applies the downgrade
# immediately, including devices whose clock is behind the server (a "now"
# timestamp would gate those out of the emergency downgrade until their clock
# caught up).

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "${SCRIPT_DIR}/lib.sh"

VERSION=""
ACTOR="${GITHUB_ACTOR:-$(whoami 2>/dev/null || echo unknown)}"
export THINR_CDN_MODE=""
export THINR_CDN_DIR=""
export THINR_S3_BUCKET=""
export THINR_CLOUDFRONT_ID=""
export THINR_DRY_RUN=0

usage() { sed -n '28,38p' "$0"; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --version)       VERSION="$2"; shift 2 ;;
        --cdn-dir)       THINR_CDN_MODE="local"; THINR_CDN_DIR="$2"; shift 2 ;;
        --bucket)        THINR_CDN_MODE="s3"; THINR_S3_BUCKET="$2"; shift 2 ;;
        --cloudfront-id) THINR_CLOUDFRONT_ID="$2"; shift 2 ;;
        --actor)         ACTOR="$2"; shift 2 ;;
        --dry-run)       THINR_DRY_RUN=1; shift ;;
        -h|--help)       usage 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage 1 ;;
    esac
done

fail() { echo "panic: error: $*" >&2; echo "panic: aborted, nothing was modified" >&2; exit 1; }

[ -n "$VERSION" ] || fail "--version is required"
[ -n "$THINR_CDN_MODE" ] || fail "pick a mode: --cdn-dir DIR or --bucket NAME"

# 1. The target must be a stable release (only stable can own `latest`).
thinr_is_stable_tag "$VERSION" || fail "version '${VERSION}' is not a stable release (must match vN.N.N)"

STAGE="$(mktemp -d)"
WORK="$(mktemp -d)"
cleanup() { rm -rf "$STAGE" "$WORK"; }
trap cleanup EXIT

# 2. Read the manifest that latest currently serves.
if ! thinr_cdn_fetch "latest.json" "${WORK}/latest.json"; then
    fail "could not read current latest.json"
fi
CUR_VERSION="$(jq -r '.version' "${WORK}/latest.json")"
echo "panic: latest currently serves ${CUR_VERSION}, target ${VERSION}, actor ${ACTOR}"

# 5. No-op if latest already points at the target.
if [ "$CUR_VERSION" = "$VERSION" ]; then
    echo "panic: latest already serves ${VERSION}; nothing to do (no-op)"
    exit 0
fi

# Architectures that latest must keep serving after the repoint.
ARCHES="$(jq -r '.checksums | keys[]' "${WORK}/latest.json")"
[ -n "$ARCHES" ] || fail "current latest.json has no checksums to match"

# 3. Validate the archived target binary for every such architecture, and stage it.
MISSING=""
for arch in $ARCHES; do
    key="binaries/${VERSION}/thinr-agent.${arch}"
    if thinr_cdn_fetch "$key" "${STAGE}/thinr-agent.${arch}"; then
        if [ ! -s "${STAGE}/thinr-agent.${arch}" ]; then
            MISSING="${MISSING} ${arch}(empty)"
        fi
    else
        MISSING="${MISSING} ${arch}(missing)"
    fi
done
if [ -n "$MISSING" ]; then
    fail "archived release ${VERSION} is incomplete for latest's architectures:${MISSING}"
fi

# 4. Compute the new checksums from the staged binaries (SHA256 must be computable).
CHECKSUMS="$(thinr_checksums_json "$STAGE")" || fail "could not compute checksums for ${VERSION}"

# Build the new manifest: soak_h=0, ramp_h=0, no frozen_at, and published_at
# pinned in the far past so clock-behind devices are not gated out of the
# emergency downgrade (the agent requires published_at > 0 and now >=
# published_at; 1 satisfies both for every sane device clock).
PUBLISHED_AT=1
jq -n \
    --arg     version      "$VERSION" \
    --argjson checksums    "$CHECKSUMS" \
    --argjson published_at "$PUBLISHED_AT" \
    '{version: $version, checksums: $checksums, published_at: $published_at, soak_h: 0, ramp_h: 0}' \
    > "${WORK}/new-latest.json"

echo "panic: repointing latest ${CUR_VERSION} -> ${VERSION}"
echo "panic: architectures: $(echo "$ARCHES" | tr '\n' ' ')"

# 8. Binaries FIRST, manifest LAST. Never --delete; versioned binaries untouched.
thinr_cdn_sync_dir "$STAGE" "binaries/latest"
thinr_cdn_put_file "${WORK}/new-latest.json" "latest.json" "application/json" "max-age=60"

# Invalidate the stable channel entry points.
thinr_cdn_invalidate "/latest.json" "/binaries/latest/*"

echo "panic: done ${CUR_VERSION} -> ${VERSION} by ${ACTOR} at $(date -u +%Y-%m-%dT%H:%M:%SZ) (published_at=${PUBLISHED_AT})"
