#!/bin/sh
# Copyright (c) Thinger.io
# SPDX-License-Identifier: MIT
#
# ThinRemote Agent Installer
# Downloads and runs the appropriate ThinRemote binary for your system

set -eu

# Configuration
BINARY_NAME="thinr-agent"
# CHANNEL is set by the generated install scripts (install.sh, install-main.sh, install-develop.sh)
# If not set, default to latest for backward compatibility
CHANNEL="${CHANNEL:-latest}"

# Detect protocol from the script download or environment
if [ -n "${HTTP_DOWNLOAD:-}" ] || echo "${0}" | grep -q "^http://"; then
    PROTOCOL="http"
else
    PROTOCOL="https"
fi

# Base URL for downloads
BASE_URL="${PROTOCOL}://get.thinremote.io"

# Initialize variables
OS=""
ARCH=""
VERSION=""

usage() {
    cat <<EOF
ThinRemote Agent Installer

Usage: install.sh [OPTIONS]

OPTIONS:
    -h, --help          Show this help message
    -v, --version       Install specific version (default: latest)
    
Examples:
    # Install latest stable version
    curl -fsSL https://get.thinremote.io/install.sh | sh
    
    # Install from main branch
    curl -fsSL https://get.thinremote.io/install-main.sh | sh
    
    # Install from develop branch
    curl -fsSL https://get.thinremote.io/install-develop.sh | sh
    
    # Install via HTTP (for devices without HTTPS)
    curl -fsSL http://get.thinremote.io/install.sh | sh

EOF
    exit 0
}

detect_os() {
    case "$(uname -s)" in
        Linux)
            OS="linux"
            ;;
        Darwin)
            OS="darwin"
            ;;
        *)
            echo "Error: Unsupported operating system: $(uname -s)"
            echo "Supported systems: Linux, macOS"
            exit 1
            ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)
            ARCH="x86_64"
            ;;
        aarch64|arm64)
            ARCH="aarch64"
            ;;
        armv7l|armv7|armhf)
            ARCH="armv7"
            ;;
        armv6l|armv6)
            ARCH="armv6"
            ;;
        i386|i686)
            ARCH="i386"
            ;;
        mips)
            # Detect endianness and float ABI for MIPS
            # First detect endianness
            ENDIAN="big"
            
            # Method 1: Check for known little-endian SoCs
            if [ -f /proc/cpuinfo ] && grep -q -i "MT7628\|mt7628\|MT7688\|mt7688\|MT762\|mt762\|Ralink\|MediaTek" /proc/cpuinfo; then
                ENDIAN="little"
            fi
            
            # Method 2: Use hexdump if available to check a binary
            if [ "$ENDIAN" = "big" ] && command -v hexdump >/dev/null 2>&1; then
                # Check ELF header of busybox or any system binary
                if [ -f /bin/busybox ]; then
                    ELF_HEADER=$(hexdump -n 5 -C /bin/busybox 2>/dev/null | head -1 | awk '{print $6}')
                    if [ "$ELF_HEADER" = "01" ]; then
                        ENDIAN="little"
                    fi
                fi
            fi
            
            # Now check for FPU
            if [ -f /proc/cpuinfo ]; then
                if ! grep -q -i "fpu\|float" /proc/cpuinfo && ! grep -q "Options.*fpu" /proc/cpuinfo; then
                    # No FPU detected, use soft float
                    echo "Note: No FPU detected, using soft-float binary"
                    if [ "$ENDIAN" = "little" ]; then
                        ARCH="mipsel-sf"
                    else
                        ARCH="mips-sf"
                    fi
                else
                    if [ "$ENDIAN" = "little" ]; then
                        ARCH="mipsel"
                    else
                        ARCH="mips"
                    fi
                fi
            else
                # Default based on endianness
                if [ "$ENDIAN" = "little" ]; then
                    ARCH="mipsel"
                else
                    ARCH="mips"
                fi
            fi
            ;;
        mipsel)
            # Little-endian MIPS
            if [ -f /proc/cpuinfo ]; then
                # Check for soft float (no FPU)
                if ! grep -q -i "fpu\|float" /proc/cpuinfo && ! grep -q "Options.*fpu" /proc/cpuinfo; then
                    # No FPU detected, use soft float
                    echo "Note: No FPU detected, using soft-float binary"
                    ARCH="mipsel-sf"
                else
                    ARCH="mipsel"
                fi
            else
                # Default to hard float if we can't detect
                ARCH="mipsel"
            fi
            ;;
        *)
            echo "Error: Unsupported architecture: $(uname -m)"
            echo "Supported architectures: x86_64, aarch64, armv7, armv6, i386, mips, mipsel"
            exit 1
            ;;
    esac
}

detect_libc() {
    # For Linux, always use musl static builds for maximum compatibility
    if [ "$OS" = "linux" ]; then
        LIBC="musl"
    fi
}

check_prerequisites() {
    # Check for curl or wget
    if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
        echo "Error: This installer requires either curl or wget."
        echo "Please install one of them and try again."
        exit 1
    fi
}

get_download_tool() {
    if command -v curl >/dev/null 2>&1; then
        echo "curl -fsSL"
    else
        echo "wget -q -O-"
    fi
}

download_file() {
    url="$1"
    output="$2"
    
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$output"
    else
        wget -q "$url" -O "$output"
    fi
}

get_latest_version() {
    echo "Fetching latest version..." >&2
    
    DOWNLOAD_TOOL=$(get_download_tool)
    
    # When using CDN, get the latest version from the version file
    if [ "$CHANNEL" = "latest" ] || [ "$CHANNEL" = "main" ] || [ "$CHANNEL" = "develop" ]; then
        # For branch channels, version is not needed
        echo "$CHANNEL"
    else
        # For specific versions (tags), use the channel as version
        echo "$CHANNEL"
    fi
}

construct_binary_name() {
    if [ "$OS" = "linux" ]; then
        # Map our arch detection to the actual binary names
        case "$ARCH" in
            x86_64)
                echo "${BINARY_NAME}.x86_64-linux-musl"
                ;;
            i386)
                echo "${BINARY_NAME}.i686-linux-musl"
                ;;
            armv6)
                echo "${BINARY_NAME}.armv5l-linux-musleabi"
                ;;
            armv7)
                echo "${BINARY_NAME}.armv7-linux-musleabihf"
                ;;
            aarch64)
                echo "${BINARY_NAME}.aarch64-linux-musl"
                ;;
            mips|mips-sf)
                echo "${BINARY_NAME}.mips-linux-musl"
                ;;
            mipsel|mipsel-sf)
                echo "${BINARY_NAME}.mipsel-linux-musl"
                ;;
            *)
                echo "Error: No binary available for architecture: $ARCH"
                exit 1
                ;;
        esac
    else
        echo "${BINARY_NAME}-${OS}-${ARCH}"
    fi
}

main() {
    # Parse command line arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                usage
                ;;
            -v|--version)
                shift
                if [ $# -eq 0 ]; then
                    echo "Error: -v requires a version argument"
                    exit 1
                fi
                VERSION="$1"
                ;;
            *)
                echo "Error: Unknown option: $1"
                usage
                ;;
        esac
        shift
    done
    
    echo "ThinRemote Agent Installer"
    echo "========================="
    echo
    
    # Detect system information
    detect_os
    detect_arch
    detect_libc
    check_prerequisites
    
    echo "System detected:"
    echo "  OS: $OS"
    echo "  Architecture: $ARCH"
    if [ -n "$LIBC" ]; then
        echo "  Libc: $LIBC"
    fi
    echo "  Protocol: $PROTOCOL"
    echo
    
    # Get version if not specified
    if [ -z "$VERSION" ]; then
        VERSION=$(get_latest_version)
    fi
    
    # Show appropriate message based on channel
    if [ "$CHANNEL" = "main" ] || [ "$CHANNEL" = "develop" ]; then
        echo "Channel: $CHANNEL (latest build)"
    else
        echo "Version to install: $VERSION"
    fi
    
    # Construct download URL
    BINARY_FILE=$(construct_binary_name)
    
    # Use CDN URL structure
    if [ "$CHANNEL" = "latest" ] || [ "$CHANNEL" = "main" ] || [ "$CHANNEL" = "develop" ]; then
        # For channels, use binaries/channel/filename
        DOWNLOAD_URL="${BASE_URL}/binaries/${CHANNEL}/${BINARY_FILE}"
    else
        # For specific versions (tags), use binaries/version/filename
        DOWNLOAD_URL="${BASE_URL}/binaries/${VERSION}/${BINARY_FILE}"
    fi
    
    echo "Download URL: $DOWNLOAD_URL"
    echo
    
    # Create temporary directory
    TEMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TEMP_DIR"' EXIT
    
    # Download binary
    echo "Downloading ThinRemote Agent..."
    download_file "$DOWNLOAD_URL" "$TEMP_DIR/$BINARY_NAME"
    
    if [ ! -f "$TEMP_DIR/$BINARY_NAME" ]; then
        echo "Error: Download failed!"
        echo "Please check the URL and your internet connection."
        exit 1
    fi
    
    # Make binary executable
    chmod +x "$TEMP_DIR/$BINARY_NAME"
    
    # Verify binary
    echo "Verifying binary..."
    if ! "$TEMP_DIR/$BINARY_NAME" --version >/dev/null 2>&1; then
        echo "Error: Binary verification failed!"
        echo "The downloaded binary may be incompatible with your system."
        exit 1
    fi
    
    # Run the binary
    echo
    echo "✓ ThinRemote Agent downloaded successfully!"
    echo
    echo "Starting ThinRemote Agent..."
    echo "================================"
    echo
    
    # Execute the binary
    "$TEMP_DIR/$BINARY_NAME" "$@"
    
    # The trap will clean up the temp directory when this script exits
}

# Run main function
main "$@"