cmake_minimum_required(VERSION 3.19)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PROJECT_VERSION)
project("libnova" VERSION ${PROJECT_VERSION})
message("Current ${PROJECT_NAME} version: ${CMAKE_PROJECT_VERSION}\n")


# ==============================================================================
# Build settings
# ==============================================================================

option(BUILD_SHARED_LIBS OFF)
option(NOVA_BUILD_TESTS "Build tests" OFF)
option(NOVA_BUILD_EXAMPLES "Build examples" ON)
option(NOVA_BUILD_CLIB "Build C library (required for nova_sim_py)" ON)


# ==============================================================================
# Global internal configuration
# ==============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/bin>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/bin>)
set(GENERATED_SRC_DIR "${CMAKE_BINARY_DIR}/generated")
set(PY_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nova_sim_py/binaries")

if (UNIX)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif ()

if (MSVC)
    add_compile_options(/utf-8)
endif ()


# ==============================================================================
# Dependencies
# ==============================================================================

set(THIRD_PARTY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

# 添加 include 路径
include_directories(${THIRD_PARTY_INCLUDE_DIR})

# === 添加 TBB 路径 ===
include_directories(${THIRD_PARTY_INCLUDE_DIR}/oneTBB/include)
link_directories(${THIRD_PARTY_INCLUDE_DIR}/oneTBB/lib/intel64/gcc4.8)
set(NOVA_TBB_LIBRARY "${THIRD_PARTY_INCLUDE_DIR}/oneTBB/lib/intel64/gcc4.8/libtbb.so.12.16")

include(FetchContent)



set(FMI4C_BUILD_SHARED OFF)
FetchContent_Declare(
        fmi4c
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/fmi4c
)
FetchContent_MakeAvailable(fmi4c)


# ==============================================================================
# Targets
# ==============================================================================

add_subdirectory(src)



if (NOVA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (NOVA_BUILD_TESTS)

    FetchContent_Declare(
            Catch2
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/Catch2
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_subdirectory(tests)
endif ()


# ==============================================================================
# Exports and remaining installation
# ==============================================================================

configure_package_config_file(cmake/config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/nova-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/nova
        NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/nova-config-version.cmake
        VERSION ${CMAKE_PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/nova-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/nova-config-version.cmake
        DESTINATION
        ${CMAKE_INSTALL_DATADIR}/nova)

install(
        TARGETS libnova libnovac
        EXPORT nova-targets
)
if (UNIX)
    install(FILES ${NOVA_TBB_LIBRARY}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RENAME libtbb.so.12)
endif ()
if (TARGET nova)
    install(
            TARGETS nova
            EXPORT nova-targets
    )
endif()


install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT nova-targets
        NAMESPACE nova::
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nova)
