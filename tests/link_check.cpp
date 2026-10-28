// Link test: reports whether the component of dvv_registry_linked_component is registered in this
// executable. Usage: <exe> present|absent; exit code 0 when that is what it finds.
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "dvv/base/leak_check.h"
#include "dvv/registry/component_registry.h"
#include "linked_component.h"

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2) {
        std::fputs("usage: link_check present|absent\n", stderr);
        return 2;
    }
    const bool expected = std::string_view(arguments[1]) == "present";

    int status = 1;
    {
        const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{});
        auto linked = registry->createComponent<dvv::test::ILinked>("test.linked");
        const bool present = linked.has_value() && (*linked)->answer() == 42;
        std::printf("test.linked is %s\n", present ? "present" : "absent");
        status = present == expected ? 0 : 1;
    }
    return dvv::finishWithLeakCheck(status);
}
