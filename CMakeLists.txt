cmake_minimum_required(VERSION 4.2)
project(dvv_registry VERSION 0.1.0 DESCRIPTION "dvv component registry" LANGUAGES CXX)
# Version string; a pre-release build adds its release-stage label (user decision U-M4ORDER).
set(dvv_registry_VERSION_STRING "${PROJECT_VERSION}")

# dvv-build provides the shared CMake modules and the dependency check (dvv-deps.txt); the dvv
# presets set DVV_BUILD_CMAKE_DIR to its cmake folder.
if(NOT DVV_BUILD_CMAKE_DIR OR NOT IS_ABSOLUTE "${DVV_BUILD_CMAKE_DIR}"
   OR NOT EXISTS "${DVV_BUILD_CMAKE_DIR}/DvvDeps.cmake")
    message(FATAL_ERROR "dvv_registry: DVV_BUILD_CMAKE_DIR is not set, not absolute or "
                        "has no DvvDeps.cmake (it must be dvv-build's cmake folder); configure "
                        "with a dvv-build preset (e.g. `cmake --preset vs2026`, see README.md) "
                        "or pass -DDVV_BUILD_CMAKE_DIR=<absolute path of dvv-build>/cmake")
endif()
include("${DVV_BUILD_CMAKE_DIR}/DvvDeps.cmake")
# Adds dvv-base (dvv-deps.txt) and checks its version; a stand-alone build tests only this
# repository.
dvv_dependencies()

option(DVV_BUILD_TESTS "Build the dvv tests" ${PROJECT_IS_TOP_LEVEL})

# dvv-base sets DVV_CMAKE_DIR to the shared modules (in-tree: dvv-build's cmake folder).
if(NOT DVV_CMAKE_DIR)
    message(FATAL_ERROR "dvv_base did not define DVV_CMAKE_DIR")
endif()
include("${DVV_CMAKE_DIR}/DvvCompilerOptions.cmake")
include(cmake/DvvComponents.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(dvv_registry STATIC
    src/capabilities.cpp
    src/component_info.cpp
    src/component_registry.cpp
    src/parameters.cpp)
add_library(dvv::registry ALIAS dvv_registry)
set_target_properties(dvv_registry PROPERTIES EXPORT_NAME registry)
target_include_directories(dvv_registry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dvv_registry PUBLIC dvv::base)
dvv_set_compiler_options(dvv_registry)

install(TARGETS dvv_registry EXPORT dvv_registryTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/dvv_registry)
install(EXPORT dvv_registryTargets NAMESPACE dvv:: DESTINATION ${config_dir})
configure_package_config_file(cmake/dvv_registryConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_registryConfig.cmake INSTALL_DESTINATION ${config_dir})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dvv_registryConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_registryConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dvv_registryConfigVersion.cmake
    cmake/DvvComponents.cmake
    DESTINATION ${config_dir})

if(DVV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
