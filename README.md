# ThinRemote Agent

<div align="center">
  <img src="https://img.shields.io/badge/platform-linux%20%7C%20macos-blue" alt="Platform">
  <img src="https://img.shields.io/badge/arch-x86__64%20%7C%20arm64%20%7C%20armv7%20%7C%20armv6-green" alt="Architecture">
  <img src="https://img.shields.io/badge/c%2B%2B-20-blue.svg" alt="C++ Standard">
  <img src="https://img.shields.io/github/license/Thin-Remote/thinr-agent" alt="License">
</div>

ThinRemote Agent is a lightweight, secure system monitoring and remote access agent that seamlessly connects Linux and macOS devices to the [Thinger.io](https://thinger.io) IoT platform. Built with C++20 and featuring static linking with musl libc, it provides zero-dependency deployment across a wide range of systems.

## 🚀 Quick Start

### One-line Installation

```bash
curl -fsSL https://raw.githubusercontent.com/Thin-Remote/thinr-agent/main/scripts/install.sh | sh
```

This will download and run the appropriate binary for your system, starting an interactive setup process.

### Direct Installation with Token

```bash
curl -fsSL https://raw.githubusercontent.com/Thin-Remote/thinr-agent/main/scripts/install.sh | sh -s -- install --host thin.company.com --token YOUR_TOKEN
```

## ✨ Features

- **System Monitoring**: Real-time metrics for CPU, memory, disk, network, and I/O
- **Multi-Platform**: Native support for Linux (all distributions) and macOS
- **Zero Dependencies**: Statically linked with musl libc for maximum portability
- **Auto-Installation**: Self-installing as system service (systemd, launchd, etc.)
- **Secure Communication**: TLS/SSL encrypted connection to Thinger.io platform
- **Low Resource Usage**: Minimal CPU and memory footprint
- **Device Management**: Automatic device provisioning and authentication
- **Cross-Architecture**: x86_64, ARM64, ARMv7, ARMv6, and i386 support

## 📋 System Requirements

### Supported Operating Systems
- **Linux**: Any distribution (Ubuntu, Debian, RHEL, Alpine, OpenWRT, etc.)
- **macOS**: 10.12 (Sierra) and later

### Supported Architectures
- x86_64 / amd64
- aarch64 / arm64
- armv7 / armhf
- armv6
- i386

### Supported Init Systems
- systemd
- launchd (macOS)
- OpenRC
- SysV Init
- Upstart

## 🔧 Installation Methods

### Interactive Setup (Recommended)

```bash
# Download and run the agent
curl -fsSL https://raw.githubusercontent.com/Thin-Remote/thinr-agent/main/scripts/install.sh | sh

# Or with wget
wget -qO- https://raw.githubusercontent.com/Thin-Remote/thinr-agent/main/scripts/install.sh | sh
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
wget https://github.com/Thin-Remote/thinr-agent/releases/latest/download/thinr-agent-linux-musl-x86_64
chmod +x thinr-agent-linux-musl-x86_64
./thinr-agent-linux-musl-x86_64
```

### Installation with Flags

```bash
# Install with auto-provisioning token
thinr-agent install --host your.thinger.instance --token YOUR_PROVISIONING_TOKEN

# Install with specific device ID
thinr-agent install --host your.thinger.instance --token YOUR_TOKEN --device-id custom-device-name

# Install without auto-start
thinr-agent install --host your.thinger.instance --token YOUR_TOKEN --no-start
```

## 🖥️ Usage

### Service Management

Once installed, ThinRemote runs as a system service:

```bash
# System-wide installation (requires sudo)
sudo thinr-agent status
sudo thinr-agent start
sudo thinr-agent stop
sudo thinr-agent uninstall

# User installation
thinr-agent status
thinr-agent start
thinr-agent stop
thinr-agent uninstall
```

### Configuration

Configuration is stored in:
- System-wide: `/etc/thinr-agent/config.json`
- User-specific: `~/.config/thinr-agent/config.json`

### Monitored Metrics

ThinRemote Agent collects and reports:
- **CPU**: Usage percentage, load averages, core count
- **Memory**: RAM and swap usage
- **Storage**: Disk space utilization per filesystem
- **Network**: Interface statistics and throughput
- **I/O**: Disk read/write metrics
- **System**: Hostname, OS version, kernel, uptime

## 🛠️ Building from Source

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+)
- CMake 3.11+
- OpenSSL development libraries
- Boost libraries (program_options)

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

## 🔒 Security

- All communications are encrypted using TLS/SSL
- Device authentication using secure tokens
- OAuth2 device flow for user authentication
- No passwords stored on disk (only secure device tokens)
- Automatic SSL certificate detection and validation

## 📝 Configuration

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

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request. For major changes, please open an issue first to discuss what you would like to change.

## 📜 License

<img align="right" src="http://opensource.org/trademarks/opensource/OSI-Approved-License-100x137.png">

This project is licensed under the [MIT License](LICENSE):

Copyright &copy; [Thinger.io](http://thinger.io)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## 🔗 Links

- [Thinger.io Platform](https://thinger.io)
- [Documentation](https://docs.thinger.io)
- [Community Forum](https://community.thinger.io)
- [Issue Tracker](https://github.com/Thin-Remote/thinr-agent/issues)