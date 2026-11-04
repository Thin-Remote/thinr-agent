#!/usr/bin/env bash
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# Classify a git ref/tag against the single shared "stable release" rule.
# Used by build.yml (publication) and release.yml (GitHub release) so both
# agree on what is a pre-release. The rule itself lives in lib.sh.
#
# Usage:
#   channel.sh is-stable <tag>     exit 0 if <tag> is a stable release, else 1
#   channel.sh classify  <ref>     print channel decision for a push ref,
#                                   as KEY=VALUE lines suitable for $GITHUB_OUTPUT
#
# classify understands refs/tags/<tag>, refs/heads/main, refs/heads/develop and
# bare tag/branch names.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "${SCRIPT_DIR}/lib.sh"

cmd="${1:-}"

case "$cmd" in
    is-stable)
        thinr_is_stable_tag "${2:-}"
        exit $?
        ;;
    classify)
        ref="${2:-}"
        name="${ref#refs/tags/}"
        name="${name#refs/heads/}"
        if [ "$ref" = "refs/tags/${name}" ] || { [ "$ref" = "$name" ] && printf '%s' "$name" | grep -Eq '^v'; }; then
            # Tag push (or bare tag name beginning with v).
            if thinr_is_stable_tag "$name"; then
                echo "channel=${name}"
                echo "is_release=true"
                echo "is_prerelease=false"
                echo "install_script=install.sh"
            else
                echo "channel=${name}"
                echo "is_release=false"
                echo "is_prerelease=true"
                echo "install_script="
            fi
        elif [ "$name" = "main" ]; then
            echo "channel=main"
            echo "is_release=false"
            echo "is_prerelease=false"
            echo "install_script=install-main.sh"
        elif [ "$name" = "develop" ]; then
            echo "channel=develop"
            echo "is_release=false"
            echo "is_prerelease=false"
            echo "install_script=install-develop.sh"
        else
            echo "error: unclassifiable ref '${ref}'" >&2
            exit 1
        fi
        ;;
    ''|-h|--help|help)
        sed -n '3,15p' "$0"
        [ "$cmd" = "" ] && exit 1 || exit 0
        ;;
    *)
        echo "error: unknown command '${cmd}'" >&2
        exit 1
        ;;
esac
