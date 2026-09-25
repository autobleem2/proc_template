# proc_template: a skeleton AutoBleem scanner processor. A processor is a plain console program: no
# AutoBleem SDK, no SDL, nothing but the C++ standard library (and whatever third-party code your own
# processor needs, vendored under third_party/ - see proc_unzip's for the shape), so it builds with any
# toolchain (the launcher's docs/scanner-processors-plan.md).
cmake_minimum_required(VERSION 3.12)
project(proc_template C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# every non-psc build (desktop/PC-stick) gets an optimized build by default: with no -DCMAKE_BUILD_TYPE,
# CMake otherwise leaves every flag unset. psc sets its own -Os below regardless of build type, so it's
# excluded - remove this whole AB_TARGET branch (and the one below) if your processor has no hot path that
# needs it, or keep it: it costs nothing on a build that never sets -Os.
if (NOT AB_TARGET STREQUAL "psc" AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# the console's Cortex-A35 (ARMv8-A, AArch32, NEON), as the launcher builds for it
if (AB_TARGET STREQUAL "psc")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 8.0)
        set(CMAKE_CXX_FLAGS "-mfloat-abi=hard -march=armv8-a+simd -Os -s")
    else()
        set(CMAKE_CXX_FLAGS "-mfloat-abi=hard -march=armv8-a -mfpu=neon-vfpv4 -Os -s")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# if your processor uses std::thread anywhere (fsutil itself does not), link Threads::Threads below - glibc
# needs pthread linked explicitly (MinGW's libstdc++ does not, so this is easy to miss until a Linux/cross
# build fails at link time with "undefined reference to pthread_create"):
# find_package(Threads REQUIRED)

# the name processor.ini's Exec= runs: bin/{key}/proc_template
add_executable(proc_template src/main.cpp src/fsutil.cpp src/strutil.cpp)
if (MINGW OR WIN32)
    # one file to copy onto the stick: no libstdc++/libgcc DLLs next to it
    target_link_options(proc_template PRIVATE -static)
    # CommandLineToArgvW (fsutil's utf8Args)
    target_link_libraries(proc_template PRIVATE shell32)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(proc_template PRIVATE -Wall -Wextra)
endif()
# target_link_libraries(proc_template PRIVATE Threads::Threads)   # pair with find_package(Threads) above

# the self-test (tests/selftest.cpp)
option(PROC_BUILD_TESTS "build and register the self-test" ON)
# a cross build runs it only where the build machine can run the result: the i386 PC stick
if (PROC_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest.cpp"
    AND (NOT CMAKE_CROSSCOMPILING OR AB_TARGET STREQUAL "pcusb"))
    enable_testing()
    add_executable(proc_template_selftest tests/selftest.cpp)
    target_compile_definitions(proc_template_selftest PRIVATE PROC_EXE="$<TARGET_FILE:proc_template>")
    add_dependencies(proc_template_selftest proc_template)
    add_test(NAME proc_template_selftest COMMAND proc_template_selftest WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
