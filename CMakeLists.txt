cmake_minimum_required(VERSION 3.20)

project(sysmon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(sysmon_core STATIC
    src/types.cpp
    src/util.cpp
    src/cpu.cpp
    src/system.cpp
    src/dashboard.cpp
)

target_include_directories(sysmon_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sysmon_core PUBLIC
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
    )
endif()

add_executable(sysmon src/main.cpp)
target_link_libraries(sysmon PRIVATE sysmon_core)
target_compile_definitions(sysmon PRIVATE SYSMON_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS sysmon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
