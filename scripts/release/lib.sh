#!/usr/bin/env bash
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# Shared helpers for the staged-release tooling of thinr-agent:
# channel classification, manifest field validation, checksum generation and a
# small CDN backend abstraction (S3 for CI, a local directory for testing).
#
# Sourced by channel.sh, gen-manifest.sh, panic.sh and freeze.sh. It performs no
# action on its own and never touches AWS unless THINR_CDN_MODE=s3 is requested
# by the caller.
#
# Works on bash 3.2 (macOS) and bash 5 (ubuntu-latest): no associative arrays,
# no bashisms newer than 3.2.

# --- Single source of truth: what counts as a "stable" release tag. ----------
#
# Stable       = tag matching exactly vN.N.N (three numeric components, no suffix)
# Pre-release  = anything else carrying a suffix: vN.N.N-rc1, -beta, -alpha.N, ...
#
# This regex is THE definition shared by publication (build.yml), the GitHub
# release job (release.yml), the panic action and the docs. Change it here only.
THINR_STABLE_TAG_REGEX='^v[0-9]+\.[0-9]+\.[0-9]+$'

# thinr_is_stable_tag <tag>  -> exit 0 if stable, 1 otherwise
thinr_is_stable_tag() {
    [ -n "${1:-}" ] || return 1
    printf '%s' "$1" | grep -Eq "$THINR_STABLE_TAG_REGEX"
}

# thinr_validate_uint <name> <value>  -> exit 0 if non-negative integer
thinr_validate_uint() {
    local name="$1" val="$2"
    case "$val" in
        ''|*[!0-9]*)
            echo "error: $name must be a non-negative integer, got: '$val'" >&2
            return 1
            ;;
    esac
    return 0
}

# thinr_norm_uint <value>  -> echo the value as a base-10 integer (no octal, no
# leading zeros) so it is a valid JSON number. Assumes it already validated.
thinr_norm_uint() {
    printf '%s' "$((10#$1))"
}

# thinr_sha256 <file>  -> echo the lowercase hex digest
thinr_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# thinr_checksums_json <dir>  -> echo a JSON object {"<suffix>":"sha256:<hex>",...}
# built from every thinr-agent.* binary in <dir>. Fails if none are found.
thinr_checksums_json() {
    local dir="$1"
    local acc='{}'
    local b suffix sha found=0
    for b in "$dir"/thinr-agent.*; do
        [ -f "$b" ] || continue
        found=1
        suffix="$(basename "$b" | sed 's/^thinr-agent\.//')"
        sha="$(thinr_sha256 "$b")"
        acc="$(printf '%s' "$acc" | jq --arg k "$suffix" --arg v "sha256:$sha" '. + {($k): $v}')"
    done
    if [ "$found" -eq 0 ]; then
        echo "error: no thinr-agent.* binaries found in $dir" >&2
        return 1
    fi
    printf '%s' "$acc"
}

# ---------------------------------------------------------------------------
# CDN backend abstraction.
#
# The same business logic drives real S3 and a local directory so panic.sh and
# freeze.sh can be exercised end to end without AWS credentials.
#
#   THINR_CDN_MODE=local  THINR_CDN_DIR=/path/to/cdnroot
#   THINR_CDN_MODE=s3     THINR_S3_BUCKET=bucket  [THINR_CLOUDFRONT_ID=id]
#
# THINR_DRY_RUN=1 makes every mutating call print its plan without executing it,
# in either mode.
# ---------------------------------------------------------------------------

# thinr_cdn_fetch <key> <dest>  -> copy remote key into local file, 1 if missing
thinr_cdn_fetch() {
    local key="$1" dest="$2"
    if [ "${THINR_CDN_MODE}" = "local" ]; then
        [ -f "${THINR_CDN_DIR}/${key}" ] || return 1
        cp "${THINR_CDN_DIR}/${key}" "$dest"
    else
        aws s3 cp "s3://${THINR_S3_BUCKET}/${key}" "$dest" >/dev/null 2>&1 || return 1
    fi
}

# thinr_cdn_exists <key>  -> exit 0 if the remote key exists
thinr_cdn_exists() {
    local key="$1"
    if [ "${THINR_CDN_MODE}" = "local" ]; then
        [ -f "${THINR_CDN_DIR}/${key}" ]
    else
        aws s3 ls "s3://${THINR_S3_BUCKET}/${key}" >/dev/null 2>&1
    fi
}

# thinr_cdn_put_file <src> <key> <content-type> <cache-control>
thinr_cdn_put_file() {
    local src="$1" key="$2" ctype="$3" cache="$4"
    if [ "${THINR_DRY_RUN:-0}" = "1" ]; then
        echo "[dry-run] put ${src} -> ${key} (content-type=${ctype:-n/a}, cache-control=${cache})"
        return 0
    fi
    if [ "${THINR_CDN_MODE}" = "local" ]; then
        mkdir -p "$(dirname "${THINR_CDN_DIR}/${key}")"
        cp "$src" "${THINR_CDN_DIR}/${key}"
    else
        if [ -n "$ctype" ]; then
            aws s3 cp "$src" "s3://${THINR_S3_BUCKET}/${key}" \
                --cache-control "$cache" --content-type "$ctype"
        else
            aws s3 cp "$src" "s3://${THINR_S3_BUCKET}/${key}" \
                --cache-control "$cache"
        fi
    fi
}

# thinr_cdn_sync_dir <src-dir> <key-dir>  -> upload a directory, NEVER --delete
thinr_cdn_sync_dir() {
    local src="$1" key="$2"
    if [ "${THINR_DRY_RUN:-0}" = "1" ]; then
        echo "[dry-run] sync ${src}/ -> ${key}/ (no --delete)"
        return 0
    fi
    if [ "${THINR_CDN_MODE}" = "local" ]; then
        mkdir -p "${THINR_CDN_DIR}/${key}"
        cp "$src"/* "${THINR_CDN_DIR}/${key}"/
    else
        aws s3 sync "$src" "s3://${THINR_S3_BUCKET}/${key}" --cache-control "max-age=3600"
    fi
}

# thinr_cdn_invalidate <path> [<path> ...]  -> CloudFront invalidation
thinr_cdn_invalidate() {
    if [ "${THINR_DRY_RUN:-0}" = "1" ]; then
        echo "[dry-run] invalidate CloudFront paths: $*"
        return 0
    fi
    if [ "${THINR_CDN_MODE}" = "local" ]; then
        echo "[local] would invalidate CloudFront paths: $*"
        return 0
    fi
    if [ -z "${THINR_CLOUDFRONT_ID:-}" ]; then
        echo "warning: THINR_CLOUDFRONT_ID not set, skipping CloudFront invalidation" >&2
        return 0
    fi
    aws cloudfront create-invalidation \
        --distribution-id "${THINR_CLOUDFRONT_ID}" \
        --paths "$@"
}
