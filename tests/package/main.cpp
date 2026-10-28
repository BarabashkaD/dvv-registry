#include <cstdio>

#include "component.h"
#include "dvv/registry/component_registry.h"

int main() {
    const auto registry = dvv::ComponentRegistry::create(dvv::Capabilities{"gpu.compute"});
    const auto greeting = registry->createComponent<IGreeting>("consumer.greeting");
    if (!greeting) {
        std::printf("%s\n", greeting.error().describe().c_str());
        return 1;
    }
    std::puts((*greeting)->text());
    return 0;
}
