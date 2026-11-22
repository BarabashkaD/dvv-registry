# dvv-registry

Component registry of the dvv stack used by rawreel: component info (`ComponentInfo`),
capability probing (`Capabilities`, `require`), loadable modules (`DVV_REGISTER_MODULE`) and the
self-registering `ComponentRegistry` that loads every module and creates every component. C++23;
depends on [dvv-base](https://github.com/BarabashkaD/dvv-base).

## Build

Builds and tests on Windows (Visual Studio 2026), Linux (Clang in the dvv-build CI image) and macOS
(Apple Silicon) with [dvv-build](https://github.com/BarabashkaD/dvv-build), which provides the
presets, the shared CMake modules, the third-party dependencies and the CI workflow; its README
lists the prerequisites. dvv-build is cloned next to dvv-registry at the version of the
`dvv-build` line in `dvv-deps.txt`; `fetch-deps.cmake` clones dvv-base next to them at its minimum
version from the same file:

```powershell
git clone https://github.com/BarabashkaD/dvv-registry
git clone --branch v0.6.0 https://github.com/BarabashkaD/dvv-build
python -m pip install -r dvv-build/tools/requirements.txt
cmake -DREPO=dvv-registry -P dvv-build/tools/fetch-deps.cmake
./dvv-build/bootstrap.ps1
cd dvv-registry
cmake --workflow --preset vs2026
```

On Linux: `./dvv-build/bootstrap.sh`, then `cmake --workflow --preset ninja-clang`. On macOS:
`./dvv-build/bootstrap.sh`, then `cmake --workflow --preset ninja-macos`.
Applications use dvv-registry as a git submodule in their `libs/` folder, next to dvv-build.

## Writing a component

A component implements an interface (a class with a virtual destructor) and registers itself. Its
interface is its category: `list<IGreeting>()` lists every greeting component.

```cpp
// greeting.cpp, in the component's own static library
#include <expected>
#include <utility>

#include "dvv/registry/component_registry.h"
#include "example_module.h"   // EXAMPLE_CAPABILITY (below)
#include "greeting.h"         // IGreeting

class Greeting final : public IGreeting, public dvv::Managed<Greeting> {
public:
    explicit Greeting(dvv::CreationKey<dvv::ComponentRegistry> key) : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {.id = "example.greeting", .name = "Greeting"};
    }
    static std::expected<void, dvv::Unavailable> probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {EXAMPLE_CAPABILITY});
    }
    // IGreeting's functions ...
};
DVV_REGISTER(IGreeting, Greeting);
```

## Writing a module

A capability is a name that one module defines as a constant and provides when it loads:

```cpp
// example_module.h
#pragma once

#include <string_view>

inline constexpr std::string_view EXAMPLE_CAPABILITY = "example.greeting-ready";
```

```cpp
// example_module.cpp, in a static component library
#include <array>
#include <string>
#include <string_view>

#include "dvv/registry/module.h"
#include "example_module.h"

namespace {
constexpr std::array<std::string_view, 1> PRODUCES{EXAMPLE_CAPABILITY};

struct ExampleModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "example", .name = "Example", .produces = PRODUCES};
    }
    // Open what the module needs; options are this module's entry of ModuleOptions.
    static dvv::ModuleLoad load(const dvv::ModuleValues& /*options*/) {
        return {.provides = {std::string(EXAMPLE_CAPABILITY)},
                .properties = {{std::string(dvv::MODULE_VERSION_PROPERTY), "1.0"}}};
    }
};
}  // namespace
DVV_REGISTER_MODULE(ExampleModule);
```

```cmake
add_library(greeting STATIC greeting.cpp example_module.cpp)
target_link_libraries(greeting PUBLIC dvv::registry)
dvv_link_components(my_app greeting)     # never a plain target_link_libraries for components
```

```cpp
// main.cpp
#include <print>
#include <string>

#include "dvv/registry/component_registry.h"
#include "greeting.h"   // IGreeting

int main() {
    // Each module gets its own options, by module id; a module without an entry gets none.
    auto registry = dvv::ComponentRegistry::load(
        {.modules = {{"example", {{"greeting", "Hello"}}}}});
    for (const dvv::ModuleStatus& module : registry->modules()) {
        std::println("{}: {}", module.info.name, module.available() ? "available" : "unavailable");
    }
    if (const dvv::ModuleStatus* example = registry->module("example")) {
        std::println("Example {}",
                     example->property(dvv::MODULE_VERSION_PROPERTY).value_or("(no version)"));
    }
    auto greeting = registry->createComponent<IGreeting>("example.greeting");
    return greeting ? 0 : 1;
}
```

`ComponentRegistry::create(capabilities)` runs no module and probes with the capabilities given:
use it in tests and in hosts that state their capabilities themselves.

## Rules

`DVV_REGISTER` and `DVV_REGISTER_MODULE` check the first two at compile time, except the ids and
the `produces` list (`load()` checks it):

- A component class is `final`, derives `dvv::Managed<T>` and the interface, and its constructor
  takes `dvv::CreationKey<dvv::ComponentRegistry>`. `info()` is `noexcept` and returns a unique,
  stable id; `probe()` is static.
- A module type has static `info() noexcept` and `load(const dvv::ModuleValues&)`; its options are
  text, by option name. Its id is unique among modules, and `produces` lists every capability
  `load()` may provide.
- Both macros stand at namespace scope in a `.cpp` of a static component library linked with
  `dvv_link_components()`. A library linked both ways is rejected by CMake.
- Create the registry from `main`, never during static initialisation.
- `load()` runs every module in id order. A module whose `load()` throws, whose id is registered
  twice, or that provides a capability `produces` does not list, provides nothing; the registry
  still starts. Each module's status (`modules()`, `module(id)`) holds what it provides and
  withholds, why, and its properties. The registry logs one line per module with its id and
  capability names, never the reason or a property: those can hold paths.
- The registry keeps each module's state (`ModuleLoad::handle`) and releases it in reverse load
  order when the registry is destroyed. Components can outlive the registry, so a module whose
  components need its state gives them their own `shared_ptr` to it. The registry stops no running
  work.
- A module's `load()` may run on several threads at once (two threads loading a registry each), so
  it is thread-safe, and it never calls `ComponentRegistry::load()`. The handle's deleter runs on
  the thread that destroys the registry; it must not throw and must not call into the registry.
- The registry runs each component's `probe()` once. If it throws or fails, the component is listed
  as unavailable with the reason; when a capability is missing, `Unavailable::capability` names it.
  A throwing constructor makes `createComponent()` return `Errc::Internal` and disables the
  component; `std::bad_alloc` returns `Errc::OutOfMemory`. Two components with the same id are both
  unavailable. An unknown or unavailable id is `Errc::Unsupported`.

Licence: MIT.
