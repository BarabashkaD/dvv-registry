# Changelog

All notable changes to dvv-registry. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow semantic versioning with
the dvv rules: compatible = the same 0.minor and at least the minimum version; a replaced or
unused API is removed in the next 0.x minor, with no deprecation period. This file is the release
notes (there are no GitHub Releases).

## [0.3.0] - 2026-11-22

Components have one form and module options are text: the parameter schemas and component kinds
are removed.

### Removed
- `dvv/registry/parameters.h`: `ParameterSchema`, `ParameterSchema::Builder`, `Parameter`,
  `IntParameter`, `FloatParameter`, `BoolParameter`, `EnumParameter`, `RangeParameter`,
  `ColourParameter`, `ParameterType`, `parameterTypeName`, `parameterTypeOf`, `ParameterValue`,
  `ParameterValues`, `Range` and `Colour`.
- A component's `schema()`, the `(CreationKey, const ParameterValues&)` constructor,
  `createComponent`'s values argument and `ComponentStatus::schema`. A component has one
  constructor, taking `CreationKey<ComponentRegistry>`; `createComponent<I>(id, where)` is the one
  form, `where` defaulting to the caller.
- `Kind`, `kindName`, `ComponentInfo::kind`, `InterfaceKind`, `HasInterfaceKind`, `list(Kind)` and
  the kind check. Use `list<I>()`.

### Changed
- Module options are text: `ModuleValues` (`std::map<std::string, std::string, std::less<>>`,
  `module.h`) replaces `ParameterValues` in `ModuleOptions::modules`, in a module's
  `load(const ModuleValues&)` and in the `Module` concept. A value of another type no longer
  compiles.
- `ComponentInfo` holds `id`, `name` and `version`; `ComponentStatus` holds `info` and
  `unavailable`.
- The reason of a throwing `load()`, `probe()` or constructor comes from dvv-base's
  `describeCurrentException()`; the texts are unchanged.
- Compatible means the same 0.minor and at least the minimum version; the package version file
  uses `SameMinorVersion`.
- The registry headers no longer include `json.h`, `<variant>`, `<cstddef>`, `<cstdint>` and
  `<limits>`; a consumer that relied on those includes them itself.
- Requires dvv-build 0.6.0 and dvv-base 0.6.0.

## [0.2.0] - 2026-11-21

Loadable modules: the registry loads them, keeps their state and reports their status. Components
need no parameter schema and no kind.

### Added
- `dvv/registry/module.h`: loadable modules. `DVV_REGISTER_MODULE(Type)` registers a type with
  static `info() noexcept -> ModuleInfo` and `load(const ParameterValues&) -> ModuleLoad`; with
  `ModuleStatus`, `ModuleProperty`, `ModuleOptions`, the `Module` concept and the property keys
  `MODULE_VERSION_PROPERTY`, `MODULE_LICENCE_PROPERTY`, `MODULE_DIRECTORY_PROPERTY` and
  `MODULE_VARIABLE_PROPERTY`.
- `ComponentRegistry::load(options)`: runs every module's `load()`, probes the components with the
  capabilities the modules provide, and keeps each module's status and state. The state is
  released in reverse load order when the registry is destroyed. `create(caps)` runs no module.
  `load()` logs one line per module at info level: its id and the capabilities it provides and
  withholds. An application that built a `Capabilities` set for `create(caps)` calls `load()`
  instead; the module behind an unavailable component is the one whose `ModuleStatus::withheld`
  holds the component's `Unavailable::capability`.
- `ComponentRegistry::modules()`, `module(id)` and `capabilities()`.
- `Unavailable::capability`: the missing capability; `require()` fills it.
- `DVV_REGISTER` accepts a component without `schema()` whose constructor takes only
  `CreationKey<ComponentRegistry>`.
- `createComponent(id, where)`.
- `list<I>()`: the components registered for interface `I`, sorted by id.

### Changed
- `Unavailable` has a second member and `create()` a second, defaulted parameter. A structured
  binding of `Unavailable` (`auto& [reason] = unavailable`) and taking `&ComponentRegistry::create`
  as its 0.1.1 function type (`std::unique_ptr<ComponentRegistry> (*)(const Capabilities&)`) no
  longer compile. Rebuild everything that includes the registry headers.
- Requires dvv-build 0.5.1.

### Deprecated (removed in 1.0.0)
- `ParameterSchema` with `ParameterSchema::Builder`, `Parameter`, the six `*Parameter` specs,
  `ParameterType`, `parameterTypeName` and `parameterTypeOf`; a component's `schema()`, the
  `(CreationKey, const ParameterValues&)` constructor, `createComponent`'s values argument and
  `ComponentStatus::schema`. No component declares a parameter, and source parameters travel in
  the request. `ParameterValue`, `ParameterValues`, `Range` and `Colour` stay (module options).
  Replacements:
  - A component: a constructor that takes only `CreationKey<ComponentRegistry>`, and no
    `schema()`.
  - `createComponent`'s values argument: `createComponent<I>(id)` or
    `createComponent<I>(id, where)`.
  - `ComponentStatus::schema` and the parameter types: none, since no component has parameters.
- `Kind`, `kindName`, `ComponentInfo::kind`, `InterfaceKind`, `HasInterfaceKind`, `list(Kind)` and
  the kind check: a component's interface is its category. Replacements:
  - `list(Kind::X)`: `list<I>()` for the interface `I`.
  - `.kind` in a component's `info()` and an `InterfaceKind` specialisation: none, leave them out.

### Fixed
- Leak reports name the caller of `ComponentRegistry::create` and `load` as the registry's creation
  site, instead of one line inside the registry.
- The README builds with dvv-build 0.5.1 and lists macOS.
- Comments and the README call the static object a "registration node" and `ComponentInfo`
  "component info".

## [0.1.1] - 2026-11-07

### Changed
- Built and tested on macOS (Apple Silicon); CI runs macOS jobs. Requires dvv-build 0.2.0 and
  dvv-base 0.2.0.

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
