// The second registration of test.mod.dup; test_modules.cpp holds the first one and the tests.

#include <array>
#include <string_view>

#include "dvv/registry/module.h"
#include "test_modules.h"

namespace {

constexpr std::array<std::string_view, 1> DUP_PRODUCES{"test.dup"};

struct DuplicateModule {
    static dvv::ModuleInfo info() noexcept {
        return {.id = "test.mod.dup", .name = "Duplicate (second)", .produces = DUP_PRODUCES};
    }
    static dvv::ModuleLoad load(const dvv::ParameterValues& /*options*/) {
        ++dvv::test::duplicate_module_loads;
        return {.provides = {"test.dup"}};
    }
};

DVV_REGISTER_MODULE(DuplicateModule);

}  // namespace
