#pragma once

// State shared by the test modules of test_modules.cpp and test_modules_duplicate.cpp.

#include <atomic>

namespace dvv::test {

// Counts the loads of every registration of test.mod.dup; the registry runs none of them.
inline std::atomic<int> duplicate_module_loads{0};

}  // namespace dvv::test
