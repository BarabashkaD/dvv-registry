#include <cstdio>

#include "component.h"
#include "dvv/registry/component_registry.h"

int main() {
    const auto loaded = dvv::ComponentRegistry::load();
    const auto greeting = loaded->createComponent<IGreeting>("consumer.greeting");
    if (!greeting) {
        std::printf("%s\n", greeting.error().describe().c_str());
        return 1;
    }
    std::puts((*greeting)->text());

    // create() runs no module, so the greeting lacks the capability the module provides.
    const auto bare = dvv::ComponentRegistry::create(dvv::Capabilities{});
    for (const auto& status : bare->list()) {
        if (status.info.id == "consumer.greeting") {
            const auto missing = status.unavailable.value_or(dvv::Unavailable{}).capability;
            return !status.available() && missing == "consumer.ready" ? 0 : 1;
        }
    }
    return 1;
}
