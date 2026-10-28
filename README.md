# dvv-registry

Component registry of the dvv stack used by rawreel: component descriptors (`ComponentInfo`, `Kind`),
capability probing (`Capabilities`, `require`), parameter schemas with validation, text and JSON
forms, and the self-registering `ComponentRegistry` that creates every component. C++23; depends on
[dvv-base](https://github.com/BarabashkaD/dvv-base).

## Build

Builds and tests on Windows (Visual Studio 2026) and Linux (Clang 19 with libstdc++ 14) with
[dvv-build](https://github.com/BarabashkaD/dvv-build), which provides the presets, the shared CMake
modules, the third-party dependencies and the CI workflow; its README lists the prerequisites.
dvv-build is cloned next to dvv-registry at the version of the `dvv-build` line in `dvv-deps.txt`;
`fetch-deps.cmake` clones dvv-base next to them at its minimum version from the same file:

```powershell
git clone https://github.com/BarabashkaD/dvv-registry
git clone --branch v0.1.0 https://github.com/BarabashkaD/dvv-build
python -m pip install -r dvv-build/tools/requirements.txt
cmake -DREPO=dvv-registry -P dvv-build/tools/fetch-deps.cmake
./dvv-build/bootstrap.ps1
cd dvv-registry
cmake --workflow --preset vs2026
```

On Linux: `./dvv-build/bootstrap.sh`, then `cmake --workflow --preset ninja-clang`.
Applications use dvv-registry as a git submodule in their `libs/` folder, next to dvv-build.

## Writing a component

A component implements an interface (a class with a virtual destructor) and registers itself:

```cpp
// greeting.cpp, in the component's own static library
class Greeting final : public IGreeting, public dvv::Managed<Greeting> {
public:
    Greeting(dvv::CreationKey<dvv::ComponentRegistry> key,
             [[maybe_unused]] const dvv::ParameterValues& values)
        : Managed(std::move(key)) { /* read values */ }

    static dvv::ComponentInfo info() noexcept {
        return {.id = "example.greeting", .name = "Greeting", .kind = dvv::Kind::Tool};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA =
            dvv::ParameterSchema::Builder()
                .addInt("repeat", "Repeat", {.min = 1, .max = 10, .defaultValue = 1})
                .build()
                .value();
        return SCHEMA;
    }
    static std::expected<void, dvv::Unavailable> probe(const dvv::Capabilities& caps) {
        return dvv::require(caps, {"gpu.compute"});
    }
    // IGreeting's functions ...
};
DVV_REGISTER(IGreeting, Greeting);
```

```cmake
add_library(greeting STATIC greeting.cpp)
target_link_libraries(greeting PUBLIC dvv::registry)
dvv_link_components(my_app greeting)     # never a plain target_link_libraries for components
```

```cpp
auto registry = dvv::ComponentRegistry::create(capabilities);   // in main, after static init
auto greeting = registry->createComponent<IGreeting>("example.greeting", {{"repeat", std::int64_t{3}}});
```

Rules (`DVV_REGISTER` checks the first two at compile time, except the id):

- The class is `final`, derives `dvv::Managed<T>` and the interface, and its constructor takes
  `dvv::CreationKey<dvv::ComponentRegistry>` and the parameter values.
- `info()` is `noexcept` and returns a unique, stable id; `schema()` and `probe()` are static.
- `DVV_REGISTER(Interface, Type);` stands at namespace scope in a `.cpp` of a static component
  library linked with `dvv_link_components()`. A library linked both ways is rejected by CMake.
- Create the `ComponentRegistry` from `main`, never during static initialisation.
- The registry runs `schema()` and `probe()` once. If one of them throws, or `probe()` fails, the
  component is listed as unavailable with the reason. A throwing constructor makes
  `createComponent()` return `Errc::Internal` and disables the component; `std::bad_alloc` returns
  `Errc::OutOfMemory`. Two components with the same id are both unavailable.
- `createComponent()` validates the values against the schema and fills in the defaults; bad values
  are `Errc::Format`, an unknown or unavailable id `Errc::Unsupported`.

## Kind check

An interface can name the component kind every implementation must report by specialising
`dvv::InterfaceKind<I>` (`dvv/registry/component_info.h`) in the interface's own header:

```cpp
// dvv/media/source_factory.h, which declares ISourceProvider
template <>
struct dvv::InterfaceKind<dvv::media::ISourceProvider>
    : std::integral_constant<dvv::Kind, dvv::Kind::Source> {};
```

A component of that interface whose `info().kind` differs is listed as unavailable, without a
probe, with the reason "kind <info kind> does not match interface kind <interface kind>" (e.g.
"kind Tool does not match interface kind Source"). Interfaces without a specialisation are not
checked.

Licence: MIT.
