# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- stations/draws-pi: plan, Pi config, PTT/TX/RX helpers and hands-on checklist for a Raspberry Pi 4 + NW Digital Radio DRAWS second HF station (FT-857D). No modem code; keying needs an enable file on the Pi.
- Initial open source release
- Communications Processor (CP) - Qt6 message processor
- Station Mapper Linux (SMLinux) - NCS operations tool
- Propagation module - VOACAP-based HF coverage prediction
- GitHub Actions CI/CD with artifact attestation
- Semantic versioning with build metadata

### Changed
- Removed the crypto module and replaced the cipher interface headers with Phoenix Nest's own; this build is plain text only
- Ported CP to Qt6 compatibility
- Removed QTextToSpeech dependency (unused)

### Fixed
- QtConcurrent::run syntax for Qt6
- Removed deprecated AA_EnableHighDpiScaling

## [1.0.0] - TBD

### Added
- First stable release

---

## Versioning Scheme

**Format**: `MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]`

- **Release builds**: `1.0.0`
- **Pre-releases**: `1.0.0-alpha`, `1.0.0-beta.1`, `1.0.0-rc.1`
- **Dev builds**: `1.0.0-dev+abc1234.42` (commit + build number)

**Tags**:
- `v1.0.0` - Stable release
- `v1.0.0-alpha` - Alpha pre-release
- `v1.0.0-beta.1` - Beta pre-release
- `v1.0.0-rc.1` - Release candidate
