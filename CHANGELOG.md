# Changelog

All notable changes to dvv-registry. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow semantic versioning with
the dvv rules: compatible = the same major and at least the minimum version, also for 0.x; breaking
changes only in a new major, after a deprecation. This file is the release notes (there are no
GitHub Releases).

## [0.1.0] - 2026-10-28

First versioned release.

### Added
- Component descriptors (`ComponentInfo`, `Kind`) and the interface kind check (`InterfaceKind`).
- Capability probing (`Capabilities`, `require`).
- Parameter schemas with validation, defaults, and text and JSON forms.
- The self-registering `ComponentRegistry` (`DVV_REGISTER`) that creates every component, with
  compile-time checks of the component class.
- The CMake package `dvv_registry` (`find_package(dvv_registry 0.1)`, `SameMajorVersion`) with
  `dvv_link_components()`; it requires dvv-base 0.1.0 or later in the 0.x series.
