cmake_minimum_required(VERSION 3.20)

project(swd2_rewrite VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
add_subdirectory(src)

if(BUILD_TESTING)
    add_test(
        NAME completion_manifest
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit-completion.py"
                --validate
    )
    add_test(
        NAME runtime_source_audit
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit-runtime-sources.py"
    )
endif()
