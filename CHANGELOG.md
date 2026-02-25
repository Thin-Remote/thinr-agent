# Changelog
All notable changes to this project will be documented in this file.

## [1.1.0] - 2026-02-25

### Added
- Remote self-update via IOTMP `update` resource
  - Check/apply actions for safe fleet-wide update management
  - SHA256 checksum verification of downloaded binaries
  - Configurable channels (latest, main, develop)
  - Platform-specific binary suffix for CDN downloads
- System monitoring extension with CPU, memory, disk, network, and load metrics
- Agent version and system_info IOTMP resources
- CI generates per-channel JSON metadata with version and checksums

## [1.0.0] - 2026-02-24

### Initial Release
- Interactive setup wizard with multiple authentication methods (password, device flow, auto-provision, direct credentials)
- CLI install command for non-interactive provisioning with auto-provision tokens
- Service management menu with update detection and in-place binary update
- Multi-platform service installers: systemd, openrc, sysv/procd, upstart, launchd
- OpenWrt/BusyBox compatibility
- Device conflict resolution during provisioning
- Structured error handling with result types for authentication flows
- SSL verification fallback for self-signed certificates
- Cross-compiled static binaries for x86_64, i386, i686, armv5, armv7, aarch64, mips, mipsel, and macOS arm64
