find_package(doctest 2.5 REQUIRED CONFIG)

add_executable(dvv_registry_tests
    main.cpp
    test_capabilities.cpp
    test_component_registry.cpp
    test_interface_kind.cpp
    test_parameter_io.cpp
    test_parameters.cpp)
target_link_libraries(dvv_registry_tests PRIVATE dvv::registry doctest::doctest)
# doctest otherwise only forward-declares std::ostream, which breaks printing std types.
target_compile_definitions(dvv_registry_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
dvv_set_compiler_options(dvv_registry_tests)
add_test(NAME dvv_registry.unit COMMAND dvv_registry_tests)
# The timeout turns a hang into a failure long before the CI job limit.
set_tests_properties(dvv_registry.unit PROPERTIES
    TIMEOUT 300
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")
# Cycle labels: `sanity` marks the fast inner-loop subset (ctest --preset vs2026-sanity); `package`
# marks install and consumer tests, which only the full cycle runs (vs2026-small excludes them).
set_property(TEST dvv_registry.unit APPEND PROPERTY LABELS sanity)

# Link tests (spec §9.1): a component in its own static library, which nothing references by
# symbol, is registered when linked with dvv_link_components() and dropped by a plain link. The
# plain link is the control: it shows that the whole-archive test proves something.
add_library(dvv_registry_linked_component STATIC linked_component.cpp)
target_link_libraries(dvv_registry_linked_component PUBLIC dvv::registry)
dvv_set_compiler_options(dvv_registry_linked_component)
add_executable(dvv_registry_link_whole link_check.cpp)
dvv_link_components(dvv_registry_link_whole dvv_registry_linked_component)
add_executable(dvv_registry_link_plain link_check.cpp)
target_link_libraries(dvv_registry_link_plain PRIVATE dvv_registry_linked_component)
foreach(variant IN ITEMS whole plain)
    dvv_set_compiler_options(dvv_registry_link_${variant})
endforeach()
add_test(NAME dvv_registry.link.whole_archive COMMAND dvv_registry_link_whole present)
add_test(NAME dvv_registry.link.plain COMMAND dvv_registry_link_plain absent)
set_tests_properties(dvv_registry.link.whole_archive dvv_registry.link.plain PROPERTIES
    TIMEOUT 60
    ENVIRONMENT DVV_LEAK_CHECK=fail
    ENVIRONMENT_MODIFICATION "${DVV_TEST_ENVIRONMENT}")

# Package test (spec §3.1): install, then build a consumer with find_package(dvv_registry) and
# another with dvv_registry in-tree. The whole build tree is installed (CMAKE_BINARY_DIR), so the
# prefix also holds dvv_base, which dvv_dependencies() always adds as a subproject. Skipped for
# sanitizer builds, whose consumers would need the same runtime flags.
if(NOT DVV_SANITIZE)
    string(REPLACE ";" "," deps_prefix_path "${CMAKE_PREFIX_PATH}")
    add_test(NAME dvv_registry.package
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-prefix
                     -DSOURCE_DIR=${dvv_registry_SOURCE_DIR}
                     -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package
                     -DCONSUMER_BUILD=${CMAKE_CURRENT_BINARY_DIR}/package-consumer
                     -DCONSUMER_TREE_BUILD=${CMAKE_CURRENT_BINARY_DIR}/pkg-tree
                     -DGENERATOR=${CMAKE_GENERATOR}
                     -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
                     -DDEPS_PREFIX_PATH=${deps_prefix_path}
                     -DDVV_BUILD_CMAKE_DIR=${DVV_BUILD_CMAKE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/package/run-package-test.cmake)
    # Package and compile-fail tests of every dvv repository drive the same build tree in a
    # dependent build with DVV_TEST_DEPENDENCIES, so they share one lock name.
    set_tests_properties(dvv_registry.package PROPERTIES
        RESOURCE_LOCK dvv_build_tree
        LABELS package)
endif()

# Compile-fail tests (spec §9.4). Case 0 is the control and must build; cases 1-5 must fail to
# compile with the expected diagnostic: a forged registry key, construction without a key, and
# DVV_REGISTER of a class that is not final, takes another authority's key or lacks probe().
set(compile_fail_source ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/direct_construction.cpp)
add_executable(dvv_registry_compile_ok ${compile_fail_source})
target_compile_definitions(dvv_registry_compile_ok PRIVATE DVV_COMPILE_FAIL_CASE=0)
target_link_libraries(dvv_registry_compile_ok PRIVATE dvv::registry)
dvv_set_compiler_options(dvv_registry_compile_ok)
if(MSVC)
    set(compile_fail_patterns "C2248" "C2665")
else()
    set(compile_fail_patterns "private constructor" "no matching constructor")
endif()
list(APPEND compile_fail_patterns
     "must be final" "must be constructible from" "must implement IWidget")
foreach(case IN ITEMS 1 2 3 4 5)
    set(target dvv_registry_compile_fail_${case})
    add_executable(${target} EXCLUDE_FROM_ALL ${compile_fail_source})
    target_compile_definitions(${target} PRIVATE DVV_COMPILE_FAIL_CASE=${case})
    target_link_libraries(${target} PRIVATE dvv::registry)
    dvv_set_compiler_options(${target})
    math(EXPR pattern_index "${case} - 1")
    list(GET compile_fail_patterns ${pattern_index} pattern)
    # cmake --build needs the top-level tree; in a dependent build (another dvv library or the app)
    # dvv-registry is a subproject.
    add_test(NAME dvv_registry.compile_fail.${case}
             COMMAND ${CMAKE_COMMAND}
                     -DBUILD_DIR=${CMAKE_BINARY_DIR}
                     -DCONFIG=$<CONFIG>
                     -DTARGET=${target}
                     "-DPATTERN=${pattern}"
                     -P ${DVV_CMAKE_DIR}/run-compile-fail.cmake)
    set_tests_properties(dvv_registry.compile_fail.${case} PROPERTIES RESOURCE_LOCK dvv_build_tree)
endforeach()
