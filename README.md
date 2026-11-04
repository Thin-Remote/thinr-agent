# ThinRemote Agent

<div align="center">
  <img src="https://img.shields.io/badge/platform-linux%20%7C%20macos-blue" alt="Platform">
  <img src="https://img.shields.io/badge/arch-x86__64%20%7C%20arm64%20%7C%20armv7%20%7C%20armv6%20%7C%20armv5%20%7C%20mips%20%7C%20riscv%20%7C%20powerpc-green" alt="Architecture">
  <img src="https://img.shields.io/badge/c%2B%2B-20-blue.svg" alt="C++ Standard">
  <img src="https://img.shields.io/github/license/Thin-Remote/thinr-agent" alt="License">
</div>

ThinRemote Agent is a lightweight, secure system monitoring and remote access agent that seamlessly connects Linux and macOS devices to the [ThinRemote](https://thinremote.io) IoT platform. It provides zero-dependency deployment across a wide range of systems and architectures.

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
- **Modbus TCP and RTU**: Read and write Modbus slaves on the local network or a serial bus, configured remotely and exposed as device resources
- **Reverse Proxy**: Access local device services through the platform
- **Self-Update**: Remote over-the-air updates with SHA256 verification
- **Multi-Platform**: Native support for Linux (all distributions) and macOS
- **Zero Dependencies**: Single binary, no external libraries required
- **Auto-Installation**: Self-installing as system service (systemd, launchd, OpenRC, SysV, Upstart)
- **Secure Communication**: TLS/SSL encrypted connection to ThinRemote platform
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

### Modbus

The agent can poll Modbus slaves on its network (Modbus TCP) or on a serial bus (Modbus RTU over RS-485/RS-232): PLCs, energy meters, inverters, drives, gateways… Devices are declared in the `modbus` section of the device's `config` property, which can be set per device or as a product property default, and applied live:

```json
{
  "modbus": {
    "devices": {
      "meter": {
        "host": "192.168.1.50",
        "port": 502,
        "unit": 1,
        "timeout": 1000,
        "points": {
          "voltage":  { "table": "input", "address": 0, "type": "float32" },
          "energy":   { "table": "holding", "address": 10, "type": "uint32", "order": "CDAB", "scale": 0.01 },
          "setpoint": { "table": "holding", "address": 100, "type": "int16", "scale": 0.1, "writable": true },
          "relay":    { "table": "coil", "address": 0, "writable": true }
        }
      },
      "drive": {
        "serial": "/dev/ttyUSB0",
        "baudrate": 19200,
        "parity": "E",
        "unit": 3,
        "points": {
          "frequency": { "address": 1, "type": "float32" }
        }
      }
    }
  }
}
```

Each device is exposed as:

- `modbus/<device>`: reads every point and returns `{point: value}`. Use it as the source of a product bucket to store the values periodically.
- `modbus/<device>/write`: present when the device has writable points. Takes `{point: value}`; all values are validated before anything is written. A `null` value leaves that point unchanged, and the resource's input template lists every writable point as `null`, so sending the template back as is writes nothing.

Point fields:

| Field | Values | Default |
|-------|--------|---------|
| `table` | `coil`, `discrete`, `holding`, `input` | `holding` |
| `address` | 0-based protocol address (register 40001 is `0`) | required |
| `type` | `bool`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `float32`, `float64`, `string` | `bool` for coils and discrete inputs, `uint16` otherwise |
| `order` | `ABCD` (big endian), `CDAB` (low word first), `BADC` (bytes swapped), `DCBA` | `ABCD` |
| `scale`, `offset` | value = raw × scale + offset (inverted on write) | `1`, `0` |
| `bit` | 0-15: a `bool` stored in one bit of a register (read-only) | |
| `length` | registers spanned by a `string` | |
| `writable` | only coils and holding registers | `false` |

Device fields:

| Field | Meaning | Default |
|-------|---------|---------|
| `host`, `port` | Modbus TCP endpoint. Devices sharing them share one connection | `host` required for TCP, `502` |
| `serial` | serial port of a Modbus RTU bus, e.g. `/dev/ttyUSB0` (prefer the stable `/dev/serial/by-id/…` names). It must resolve to a serial (tty) character device under `/dev`; anything else is refused before it is opened. All devices on a port, whatever name they use for it, share it and must use the same line settings | required for RTU |
| `baudrate`, `parity`, `data_bits`, `stop_bits` | RTU line settings; `parity` is `N`, `E` or `O` | `9600`, `N`, `8`, `1` |
| `transport` | `tcp` or `rtu` | `rtu` when `serial` is set, `tcp` otherwise |
| `unit` | unit id (slave address): 1-247 on a serial bus | `1` |
| `timeout` | per request, in ms | `1000` |
| `max_gap` | unused addresses a single read may span to merge neighbouring points (keep `0` for slaves that reject reads of unmapped addresses) | `0` |
| `interval` | seconds between pushes on `modbus/data` (`0`: not pushed) | `0` |
| `id`, `model` | sent along with the pushed data, to name and classify the device on the platform | |

Up to 128 devices are accepted; the rest are reported as configuration errors in `$modbus/status`.

#### Devices as subdevices

Devices with an `interval` are read periodically and pushed as events on the `modbus/data` resource, while the server listens to it:

```json
{ "device": "meter", "id": "aem310_SN000123", "model": "AEM310-MID", "values": { "voltage": 230.5 } }
```

A flow in the gateway's product can turn each Modbus device into a platform subdevice with its own product, buckets and alarms: source `modbus/data` (resource, update on events), a `device_id_resolver` such as

```js
function resolveSubdevice(payload, ctx) {
    return payload.id || ctx.device + '_' + payload.device;
}
```

and a `resource_stream` sink with payload `{{payload.values}}`. Products of the subdevices autoprovision them by id pattern (e.g. `^aem310_.*`), so a model prefix in `id` picks the product, as plugin devices do. The subdevice gets the gateway as its parent.

#### Commissioning tools

These resources let a person or an AI agent explore a Modbus network and build the configuration from what it finds. Tools that address a slave take either `device` (a configured one), `host`, `port` and `unit`, or `serial`, its line settings and `unit`. On a serial port used by configured devices, a tool without line settings uses theirs, and its requests take turns with theirs on the port. A `null` field counts as absent, which is how the input templates mark optional fields:

- `$modbus/scan`: without input, finds the hosts of the local networks listening on port 502 (`network` and `port` narrow it) and lists the serial ports. With `host` or `serial`, lists the unit ids answering there (`unit_from`, `unit_to`, 1-16 by default), e.g. the slaves behind a gateway or on a bus. With `serial` and `"detect": true`, also finds the bus line settings by trying the usual baud rates and parities against units 1-5 (or the given range); it needs the port not to be in use by configured devices. In a detection, `baudrate` and `parity` are filters: when given (not `null`), only that baud rate or parity is tried.
  - `timeout` (50-2000 ms, `300` by default) is how long each probe waits for an answer.
  - A scan answers within 20 seconds. When the work does not fit, it returns what it found so far with `"partial": true` and a `resume` object: send the same request again with those fields added to continue (`unit_from` for a unit scan, `start_baudrate` and `start_parity` for a detection, which also lists the settings already `tried`). A request that could not progress at all (a network scan or one detection step longer than 20 seconds) is refused with a hint to narrow it.
- `$modbus/identify`: reads the standard identification objects (vendor, product code, revision, model name…) when the slave supports them (function 43).
- `$modbus/read`: raw read of `count` coils or registers from `table` and `address`.
- `$modbus/probe`: reads up to 16 registers and decodes them as every type and byte order, to recognise how a value is encoded.
- `$modbus/status`: each configured device's endpoint, points, last successful read and last error, or why its configuration was rejected.

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