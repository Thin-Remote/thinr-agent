# Changelog
All notable changes to this project will be documented in this file.

## [1.2.1] - 2024-06-22

### Changed

- Auto provisioning from token to thinger product autoprovision

## [1.2.0] - 2024-06-18

### Changed

- Concern separation for metrics retrieval
- Updated to thinger-iotmp 1.3.0
- Cmd resource now based on thinger-iotmp
- Removed reboot and update resources

### Fix

- Removed 'new' pointer for buffer in S3 upload parts

## [1.1.0] - 2023-10-04

### Changed

- Mongodb backup is compressed in gzip by default
- Influxdb and mongodb backups are created in default mounted directory of container

### Removed

- Backup of influxdb v1 is no longer needed

## [1.0.0] - 2023-08-22

### Changed

- Set thinger-iotmp dependency as production version 1.1.0

## [0.0.9] - 2023-07-18

### Fix

- Format logs before calling log macro
- Don't fail when backing up and restoring plugins without docker images

## [0.0.8] - 2023-06-26

### Fix

- Auto provisioning of servers when hostname is less than 32 chars

## [0.0.7] - 2023-05-30

### Added

- Added verbosity level (up to 3) as parameter option
- New network resources: network total speed and network total data transfer
- Monitor resource is accessible through localhost endpoint

### Changed

- Connection to Thinger Console is done through IOTMP protocol
- Renamed `nw_default_total_incoming` to `nw_default_transfer_incoming`
- Renamed `nw_default_total_outgoing` to `nw_default_transfer_outgoing`
- Round metrics to two decimals
- Call endpoint from cmd, backup and restore resources is done through client method
- Replaced `to_json` and `to_pson` functions with the ones provided by the IOTMP firmware
- Use boost `program_options` and remove user and url as options
- No direct calls to spdlog, instead it call the IOTMP LOG macros

### Fix

- Network speed was not reporting correctly
- Network data transfer not reporting decimals
- Network, filesystem and drive stats on first retrieval are not real
- Storage metrics not reporting decimals
- Limit max device id to 32 chars
- Add libatomic for armv7 static compilation

### Security

- Integrated with OpenSSL 3

### Removed

- Resources, backups and storage properties from local configuration

## [0.0.6] - 2022-09-08

### Changed

- InfluxDB version detection for backup and restore is done through REST API
- Removed creation of TMPDIR for influxdb2 restore as it is no longer neccesary
- Improved log output using spdlog library

### Fix

- When exec command fails internally function will return false
- Restore on influxdb2 was not restarting container
- Memory swap was not being correctly monitored
- Resources were being duplicated with default and own name
- Endpoint calling in restore not sending payload

## [0.0.5] - 2022-04-25

### Added

- Backup and restore available for influxdb 2.1
- Tag configurable in backup resource
- Backup, Restore and Cmd resources may call endpoint on finished
- Added default tag and endpoint for backup and restore resources
- Added platform version as resource
- Backup and restore return json as payload with details when calling endpoint

### Fix

- Segmentation fault when checking internal interface IP
- Backup and restore resources accessible once backups property is set without restarting
- Backup and restore are now asyncronous and blocking between each other
- System update is asyncronous and blocking against backup and restore

## [0.0.4] - 2022-03-14

### Added

- New configuration options for backup and restore operations
- Added S3 upload and retrieval capabilities for backups
- Installer now allows installation of a specific version
- New distro release upgrade resource
- Check on token if device exists and change credentials of the device

### Changed

- Moved backup and restore scritps to thinger resource
- Superseded base64 class with new one
- Update and reboot resources only available when executed with root
- Service restarts by its own if it crashes
- Upload to S3 of backups is done by multipart upload instead of loading full file in memory

## [0.0.3] - 2022-01-03

### Added

- Backups and restore scripts with S3 as storage backend
- Device name in configuration file
- Kernel version as metric

### Changed

- Device name is set to hostname by default

## [0.0.2] - 2021-11-30

### Added

- Uptime as system information metric

### Changed

- Installer takes into account remote updates

### Fix

- System updates not showing
- Command execution input resource request body as JSON
- Declared input and output for update and restart resources

## [0.0.1] - 2021-11-25

### Added

- System information metrics
- CPU usage and load metrics
- RAM and swap capacity metrics
- Network public and private IPs, speeds and total transfer
- Mount points capacity metrics
- I/O drives speed and usage metrics
- Module configuration
- Execution of commands

[1.2.1]: https://github.com/thinger-io/monitoring-client/compare/1.2.0...1.2.1
[1.2.0]: https://github.com/thinger-io/monitoring-client/compare/1.1.1...1.2.0
[1.1.1]: https://github.com/thinger-io/monitoring-client/compare/1.1.0...1.1.1
[1.1.0]: https://github.com/thinger-io/monitoring-client/compare/1.0.0...1.1.0
[1.0.0]: https://github.com/thinger-io/monitoring-client/compare/0.0.9...1.0.0
[0.0.9]: https://github.com/thinger-io/monitoring-client/compare/0.0.8...0.0.9
[0.0.8]: https://github.com/thinger-io/monitoring-client/compare/0.0.7...0.0.8
[0.0.7]: https://github.com/thinger-io/monitoring-client/compare/0.0.6...0.0.7
[0.0.6]: https://github.com/thinger-io/monitoring-client/compare/0.0.5...0.0.6
[0.0.5]: https://github.com/thinger-io/monitoring-client/compare/0.0.4...0.0.5
[0.0.4]: https://github.com/thinger-io/monitoring-client/compare/0.0.3...0.0.4
[0.0.3]: https://github.com/thinger-io/monitoring-client/compare/0.0.2...0.0.3
[0.0.2]: https://github.com/thinger-io/monitoring-client/compare/0.0.1...0.0.2
[0.0.1]: https://github.com/thinger-io/monitoring-client/tag/0.0.1
