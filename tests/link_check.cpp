// Link test: reports whether the component and the module of dvv_registry_linked_component are
// registered in this executable. Usage: <exe> present|absent; exit code 0 when that is what it
// finds.
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
        const auto registry = dvv::ComponentRegistry::load();
        auto linked = registry->createComponent<dvv::test::ILinked>("test.linked");
        const bool component = linked.has_value() && (*linked)->answer() == 42;
        const bool module = registry->module("test.linked_module") != nullptr;
        std::printf("test.linked is %s\n", component ? "present" : "absent");
        std::printf("test.linked_module is %s\n", module ? "present" : "absent");
        status = (component && module) == expected ? 0 : 1;
    }
    return dvv::finishWithLeakCheck(status);
}
