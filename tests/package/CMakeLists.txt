# Consumer of dvv_registry; built by run-package-test.cmake. With DVV_REGISTRY_SOURCE_DIR it takes
# dvv_registry in-tree through FetchContent (as a dependent library or the app takes it; then
# DVV_BUILD_CMAKE_DIR names dvv-build's cmake folder), otherwise the installed package. Its
# component lives in a static library linked with dvv_link_components(), so the test also proves
# that self-registration survives linking with the installed function.
cmake_minimum_required(VERSION 4.2)
project(dvv_registry_package_consumer LANGUAGES CXX)

if(DVV_REGISTRY_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(dvv_registry SOURCE_DIR "${DVV_REGISTRY_SOURCE_DIR}")
    FetchContent_MakeAvailable(dvv_registry)
else()
    find_package(dvv_registry 0.1 REQUIRED)
endif()
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")

add_library(consumer_component STATIC component.cpp)
target_link_libraries(consumer_component PUBLIC dvv::registry)
dvv_set_compiler_options(consumer_component)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dvv::registry)
dvv_link_components(consumer consumer_component)
dvv_set_compiler_options(consumer)
