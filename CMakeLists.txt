cmake_minimum_required(VERSION 3.11)
project(thinr-agent)

# Set CMake policy for FetchContent timestamp handling
if (${CMAKE_VERSION} VERSION_GREATER "3.24")
  cmake_policy(SET CMP0135 NEW)  # Use extraction time for downloaded archives
endif()

# Handle new CMake policies for Boost
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.30")
  cmake_policy(SET CMP0167 OLD)  # Continue using FindBoost for now
endif()

# Get version from git tags
execute_process(
    COMMAND git describe --tags --always
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Get current branch
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# If git commands failed, use default version
if(NOT GIT_VERSION)
    set(GIT_VERSION "v1.0.0")
    message(WARNING "Git version not found, using default: ${GIT_VERSION}")
endif()

if(NOT GIT_BRANCH)
    set(GIT_BRANCH "unknown")
    message(WARNING "Git branch not found, using default: ${GIT_BRANCH}")
endif()

# Build version string based on branch
if(GIT_BRANCH STREQUAL "main")
    set(AGENT_VERSION "${GIT_VERSION}")
else()
    set(AGENT_VERSION "${GIT_BRANCH}-${GIT_VERSION}")
endif()

message(STATUS "Git branch: ${GIT_BRANCH}")
message(STATUS "Git version: ${GIT_VERSION}")
message(STATUS "Agent version: ${AGENT_VERSION}")

# Pass version to source code
add_definitions(-DAGENT_VERSION=\"${AGENT_VERSION}\")

# check c++20 support
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable ARM ABI change warnings
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
  add_compile_options(-Wno-psabi)
endif()

# configure build parameters
OPTION(ENABLE_OPENSSL "Enable use of OpenSSL" ON)
OPTION(STATIC "Enable static linking" OFF)
OPTION(BIN_EXTENSION "Set binary file extension" OFF)

# Boost
set(Boost_USE_MULTITHREADED      ON)
if(STATIC)
  set(Boost_USE_STATIC_LIBS ON)
else()
  set(Boost_USE_STATIC_LIBS OFF)
endif()
find_package(Boost REQUIRED COMPONENTS program_options)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  list(APPEND ADDITIONAL_LIBS ${Boost_LIBRARIES})
endif()

# OpenSSL
if(ENABLE_OPENSSL)
  SET(OPEN_SSL 1)
  if(STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
  endif()
  find_package(OpenSSL REQUIRED)
  if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    list(APPEND ADDITIONAL_LIBS OpenSSL::SSL)
    list(APPEND ADDITIONAL_LIBS OpenSSL::Crypto)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION} ${OPENSSL_INCLUDE_DIR} ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES}")
  endif()
else()
  SET(OPEN_SSL 0)
endif()


# Source files
file(GLOB_RECURSE SOURCE_FILES
        "src/*.hpp"
        "src/*.h"
        "src/*.cpp"
        "src/*.cc"
        "src/*.c"
)

include(FetchContent)

# spdlog
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY  https://github.com/gabime/spdlog.git
        GIT_TAG         v1.15.3
)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
FetchContent_MakeAvailable(spdlog)

# json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)
list(APPEND ADDITIONAL_LIBS nlohmann_json::nlohmann_json)

# httplib
FetchContent_Declare(
        httplib
        GIT_REPOSITORY  https://github.com/yhirose/cpp-httplib.git
        GIT_TAG         v0.16.0
)
FetchContent_MakeAvailable(httplib)
list(APPEND ADDITIONAL_LIBS httplib::httplib)

FetchContent_Declare(
        thinger_iotmp
        GIT_REPOSITORY https://github.com/thinger-io/IOTMP-Linux.git
        GIT_TAG        develop
)
FetchContent_MakeAvailable(thinger_iotmp)
list(APPEND ADDITIONAL_LIBS thinger_iotmp)

# ThinRemote source files
file(GLOB_RECURSE THINREMOTE_SOURCE_FILES
        "src/main.cpp"
        "src/thinr/*.hpp"
        "src/thinr/*.cpp"
        "src/thinr/**/*.hpp"
        "src/thinr/**/*.cpp"
)

# Original thin_remote executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} ${ADDITIONAL_LIBS})

# ThinRemote executable
add_executable(thinremote ${THINREMOTE_SOURCE_FILES})
target_link_libraries(thinremote ${ADDITIONAL_LIBS})

# Strip binaries in Release mode for smaller size
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Stripping ${PROJECT_NAME} binary"
    )
    add_custom_command(TARGET thinremote POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:thinremote>
        COMMENT "Stripping thinremote binary"
    )
endif()

# Export the thinger library for use in other projects
export(TARGETS nlohmann_json
        NAMESPACE nlohmann_json::
        FILE ${CMAKE_CURRENT_BINARY_DIR}/monitoringClientTargets.cmake)

export(TARGETS httplib
        NAMESPACE httplib::
        FILE ${CMAKE_CURRENT_BINARY_DIR}/httplibTargets.cmake)

export(TARGETS spdlog
        NAMESPACE spdlog::
        FILE ${CMAKE_CURRENT_BINARY_DIR}/spdlogTargets.cmake)

if(BIN_EXTENSION)
  if(ARCH)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.${ARCH}")
    message(STATUS "Binary name is ${PROJECT_NAME}.${ARCH}")
  else()
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "Binary name is ${PROJECT_NAME}.${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()