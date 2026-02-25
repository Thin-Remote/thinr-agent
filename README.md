# ThinRemote Agent

<div align="center">
  <img src="https://img.shields.io/badge/platform-linux%20%7C%20macos-blue" alt="Platform">
  <img src="https://img.shields.io/badge/arch-x86__64%20%7C%20arm64%20%7C%20armv7%20%7C%20armv6%20%7C%20armv5%20%7C%20mips%20%7C%20riscv%20%7C%20powerpc-green" alt="Architecture">
  <img src="https://img.shields.io/badge/c%2B%2B-20-blue.svg" alt="C++ Standard">
  <img src="https://img.shields.io/github/license/Thin-Remote/thinr-agent" alt="License">
</div>

ThinRemote Agent is a lightweight, secure system monitoring and remote access agent that seamlessly connects Linux and macOS devices to the [Thinger.io](https://thinger.io) IoT platform. It provides zero-dependency deployment across a wide range of systems and architectures.

## Quick Start

### One-line Installation

```bash
curl -fsSL https://get.thinremote.io/install.sh | sh
```

This will download and run the appropriate binary for your system, starting an interactive setup process.

### Install from a specific channel

```bash
# Latest stable release
curl -fsSL https://get.thinremote.io/install.sh | sh

# Main branch (latest build)
curl -fsSL https://get.thinremote.io/install-main.sh | sh

# Develop branch (bleeding edge)
curl -fsSL https://get.thinremote.io/install-develop.sh | sh
```

### Install via HTTP (for devices without HTTPS support)

```bash
curl -fsSL http://get.thinremote.io/install.sh | sh
```

### Direct Installation with Token

```bash
curl -fsSL https://get.thinremote.io/install.sh | sh -s -- install --host thin.company.com --token YOUR_TOKEN
```

## Features

- **System Monitoring**: Real-time metrics for CPU, memory, disk, network, and I/O
- **Remote Terminal**: Secure shell access to devices from the platform
- **File System**: Remote file browsing, upload, and download
- **Remote Commands**: Execute commands on devices remotely
- **Custom Scripts**: CGI-like scripting extension — drop executable scripts in a folder and they become remote-accessible resources
- **Reverse Proxy**: Access local device services through the platform
- **Self-Update**: Remote over-the-air updates with SHA256 verification
- **Multi-Platform**: Native support for Linux (all distributions) and macOS
- **Zero Dependencies**: Single binary, no external libraries required
- **Auto-Installation**: Self-installing as system service (systemd, launchd, OpenRC, SysV, Upstart)
- **Secure Communication**: TLS/SSL encrypted connection to Thinger.io platform
- **Low Resource Usage**: Minimal CPU and memory footprint
- **Device Management**: Automatic device provisioning and authentication
- **Cross-Architecture**: 16 target architectures including x86_64, ARM64, ARMv7, ARMv6, ARMv5, MIPS, RISC-V, and PowerPC

## System Requirements

### Supported Operating Systems
- **Linux**: Any distribution (Ubuntu, Debian, RHEL, Alpine, OpenWRT, etc.)
- **macOS**: 15.0 (Sequoia) and later

### Supported Architectures

| Architecture | Linux | macOS |
|---|---|---|
| x86_64 / amd64 | ✅ | — |
| aarch64 / arm64 | ✅ | ✅ |
| armv7 / armhf | ✅ | — |
| armv6 | ✅ | — |
| armv5l | ✅ | — |
| i686 | ✅ | — |
| i386 | ✅ | — |
| mips (big-endian) | ✅ | — |
| mipsel (little-endian) | ✅ | — |
| mipsel-sf (soft-float) | ✅ | — |
| mips64 | ✅ | — |
| mips64el | ✅ | — |
| powerpc | ✅ | — |
| powerpc64le | ✅ | — |
| riscv32 | ✅ | — |
| riscv64 | ✅ | — |

### Supported Init Systems
- systemd
- launchd (macOS)
- OpenRC
- SysV Init
- Upstart

## Installation Methods

### Interactive Setup (Recommended)

```bash
# Download and run the agent
curl -fsSL https://get.thinremote.io/install.sh | sh

# Or with wget
wget -qO- https://get.thinremote.io/install.sh | sh
```

The interactive setup will guide you through:
1. Authentication (OAuth2 browser flow, credentials, or token)
2. Device registration
3. Service installation
4. Automatic startup configuration

### Manual Installation

Download the appropriate binary from [releases](https://github.com/Thin-Remote/thinr-agent/releases):

```bash
# Example for Linux x86_64
wget https://github.com/Thin-Remote/thinr-agent/releases/latest/download/thinr-agent.x86_64-linux-musl
chmod +x thinr-agent.x86_64-linux-musl
./thinr-agent.x86_64-linux-musl
```

### Installation with Flags

```bash
# Install with auto-provisioning token
thinr-agent install --host your.thinr.io --token YOUR_PROVISIONING_TOKEN

# Install with specific device ID
thinr-agent install --host your.thinr.io --token YOUR_TOKEN --device-id custom-device-name

# Install without auto-start
thinr-agent install --host your.thinr.io --token YOUR_TOKEN --no-start
```

## Usage

### Service Management

Once installed, ThinRemote runs as a system service. Run the agent without arguments to access the interactive management menu:

```bash
# System-wide installation (requires sudo)
sudo thinr-agent

# User installation
thinr-agent
```

The management menu allows you to start, stop, restart, view logs, update, or uninstall the service.

You can also use direct commands:

```bash
thinr-agent uninstall      # Uninstall service and remove configuration
thinr-agent reconfigure    # Restart interactive configuration
```

### Configuration

Configuration is stored in:
- System-wide: `/etc/thinr-agent/config.json`
- User-specific: `~/.config/thinr-agent/config.json`

### Custom Scripts

Place executable scripts in the scripts directory to expose them as remote resources:

- System-wide: `/etc/thinr-agent/scripts/`
- User-specific: `~/.config/thinr-agent/scripts/`

Each script becomes an IOTMP resource accessible from the platform. Scripts receive JSON input via stdin and return JSON output via stdout.

```bash
#!/bin/bash
# /etc/thinr-agent/scripts/hello.sh

if [ "$1" = "--describe" ]; then
    echo '{"input": {"name": ""}, "output": {"greeting": ""}}'
    exit 0
fi

INPUT=$(cat)
NAME=$(echo "$INPUT" | jq -r '.name // "world"')
echo "{\"greeting\": \"Hello, $NAME!\"}"
```

### Monitored Metrics

ThinRemote Agent collects and reports:
- **CPU**: Usage percentage, load averages, core count
- **Memory**: RAM and swap usage
- **Storage**: Disk space utilization per filesystem
- **Network**: Interface statistics and throughput
- **I/O**: Disk read/write metrics
- **System**: Hostname, OS version, kernel, uptime

## Building from Source

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+)
- CMake 3.11+
- OpenSSL development libraries
- Boost libraries (program_options, iostreams, system, filesystem, process, date_time)

### Build Commands

```bash
# Clone the repository
git clone https://github.com/Thin-Remote/thinr-agent.git
cd thinr-agent

# Create build directory
mkdir build && cd build

# Configure (static build with OpenSSL)
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENSSL=ON -DSTATIC=ON ..

# Build
make -j$(nproc) thinr-agent

# Run
./thinr-agent
```

### Development Build

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_OPENSSL=ON -DSTATIC=ON ..
make -j$(nproc) thinr-agent

# Run tests
make tests
./tests
```

## Security

- All communications are encrypted using TLS/SSL
- Device authentication using secure tokens
- OAuth2 device flow for user authentication
- No passwords stored on disk (only secure device tokens)
- Automatic SSL certificate detection and validation
- Remote updates verified with SHA256 checksums

## Configuration

### Environment Variables

- `SSL_CERT_FILE`: Path to SSL certificate bundle
- `SSL_CERT_DIR`: Path to SSL certificate directory
- `THINR_CONFIG`: Custom configuration file path

### Command Line Options

```bash
thinr-agent --help                    # Show help
thinr-agent --version                 # Show version
thinr-agent --config /path/to/config  # Use custom config
thinr-agent -v                        # Verbose output
thinr-agent -vv                       # Debug output
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request. For major changes, please open an issue first to discuss what you would like to change.

## License

This project is licensed under the [MIT License](LICENSE).