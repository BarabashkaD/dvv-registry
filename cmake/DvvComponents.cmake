# Component linking (spec §9.1). Installed with dvv_registry and included by its package
# configuration; in-tree the dvv_registry project includes it.
include_guard(GLOBAL)

# dvv_link_components(<target> [<component library>...])
# Links each component library into <target> as a whole archive ($<LINK_LIBRARY:WHOLE_ARCHIVE,...>:
# MSVC /WHOLEARCHIVE, GNU --whole-archive, Apple -force_load), so the DVV_REGISTER descriptors in it
# survive linking although nothing references them. Use it for executables and shared libraries.
# Each library must be a static library target that is linked into <target> only this way (CMake
# rejects a library linked both with and without WHOLE_ARCHIVE). An empty list is allowed (every
# component switched off). With MSVC and at least one library, <target> also gets /INCREMENTAL:NO:
# an incremental link with /WHOLEARCHIVE can keep a stale binary after a component changes.
function(dvv_link_components target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "dvv_link_components: '${target}' is not a target")
    endif()
    foreach(library IN LISTS ARGN)
        if(NOT TARGET ${library})
            message(FATAL_ERROR "dvv_link_components: '${library}' is not a target")
        endif()
        get_target_property(type ${library} TYPE)
        if(NOT type STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR
                    "dvv_link_components: '${library}' is a ${type}; component libraries are static")
        endif()
        target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${library}>")
        if(MSVC)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)  # de-duplicated by CMake
        endif()
    endforeach()
endfunction()
