#pragma once

// Interface of the component in the dvv_registry_linked_component static library, which nothing
// references by symbol: only whole-archive linking keeps its registration (link tests).

namespace dvv::test {

class ILinked {
public:
    ILinked() = default;
    ILinked(const ILinked&) = delete;
    ILinked& operator=(const ILinked&) = delete;
    ILinked(ILinked&&) = delete;
    ILinked& operator=(ILinked&&) = delete;
    virtual ~ILinked() = default;

    [[nodiscard]] virtual int answer() const = 0;
};

}  // namespace dvv::test
