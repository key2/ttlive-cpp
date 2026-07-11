cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# TikTokLive C++ — cross-platform (Windows / Linux / macOS) client that
# connects to TikTok LIVE and receives (does not send) chat/gift/like/etc.
# events over the Webcast WebSocket protocol.
#
# Dependencies (resolved via find_package; on Windows use the vcpkg toolchain):
#   - Boost (Beast + Asio)          : HTTP(S) + WebSocket transport
#   - OpenSSL                       : TLS + MD5 for the signature code
#   - protobuf                      : Webcast message (de)serialization
#   - ZLIB                          : gzip decompression of WS payloads
#   - Threads
#
# vcpkg example configure:
#   cmake -B build -S . \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
# ---------------------------------------------------------------------------

project(ttlive_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# curl-impersonate: BoringSSL-based libcurl fork that reproduces Chrome's TLS /
# HTTP2 / header fingerprints. Required to get past TikTok's WAF (which blocks
# OpenSSL/Beast fingerprints). Handles both the REST fetches and the WSS push
# connection. See cmake/curl-impersonate.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/curl-impersonate.cmake)

# protobuf: prefer the CMake config (protobuf::libprotobuf + protobuf::protoc).
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Generate C++ from the Webcast .proto schema (auto-generated from upstream).
# ---------------------------------------------------------------------------
set(TTLIVE_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/tiktok.proto)
set(TTLIVE_PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TTLIVE_PROTO_OUT})

# Locate the protoc binary regardless of how protobuf was found.
if(TARGET protobuf::protoc)
  set(TTLIVE_PROTOC protobuf::protoc)
elseif(Protobuf_PROTOC_EXECUTABLE)
  set(TTLIVE_PROTOC ${Protobuf_PROTOC_EXECUTABLE})
else()
  find_program(TTLIVE_PROTOC protoc REQUIRED)
endif()

add_custom_command(
  OUTPUT ${TTLIVE_PROTO_OUT}/tiktok.pb.cc ${TTLIVE_PROTO_OUT}/tiktok.pb.h
  COMMAND ${TTLIVE_PROTOC}
          --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
          --cpp_out=${TTLIVE_PROTO_OUT}
          ${TTLIVE_PROTO_FILE}
  DEPENDS ${TTLIVE_PROTO_FILE}
  COMMENT "Generating C++ from tiktok.proto"
  VERBATIM
)

# ---------------------------------------------------------------------------
# QuickJS (bellard) — vendored as a git submodule in third_party/quickjs.
# We build it as a static lib directly from its amalgamated C sources so no
# external QuickJS install is needed on any platform.
# ---------------------------------------------------------------------------
set(QJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs)
if(NOT EXISTS ${QJS_DIR}/quickjs.c)
  message(FATAL_ERROR
    "third_party/quickjs is empty. Run:\n"
    "  git submodule update --init --recursive")
endif()

# Read the submodule's VERSION file for CONFIG_VERSION.
file(STRINGS ${QJS_DIR}/VERSION QJS_VERSION LIMIT_COUNT 1)

add_library(quickjs STATIC
  ${QJS_DIR}/quickjs.c
  ${QJS_DIR}/dtoa.c
  ${QJS_DIR}/libregexp.c
  ${QJS_DIR}/libunicode.c
  ${QJS_DIR}/cutils.c
)
# The QuickJS source tree ships a lowercase-clashing "VERSION" file. Exposing
# the whole tree on the (public) include path makes the C++ standard header
# lookup for <version> (pulled in by libc++ headers like <array>) resolve to
# that file on case-insensitive filesystems, breaking every consumer's build.
# So QuickJS compiles with the full source dir PRIVATE, and we publish only the
# public header via a dedicated, collision-free include directory.
set(QJS_PUBLIC_INC ${CMAKE_CURRENT_BINARY_DIR}/quickjs_include)
file(MAKE_DIRECTORY ${QJS_PUBLIC_INC})
configure_file(${QJS_DIR}/quickjs.h ${QJS_PUBLIC_INC}/quickjs.h COPYONLY)
target_include_directories(quickjs
  PRIVATE ${QJS_DIR}
  PUBLIC  ${QJS_PUBLIC_INC})
target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="${QJS_VERSION}")
if(NOT WIN32)
  target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
endif()
if(MSVC)
  target_compile_definitions(quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(NOT WIN32)
  target_link_libraries(quickjs PUBLIC m)
endif()

# The TikTok SDK JS files are loaded at runtime; expose their directory.
target_compile_definitions(quickjs INTERFACE
  TTLIVE_JS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/js")

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(ttlive STATIC
  ${TTLIVE_PROTO_OUT}/tiktok.pb.cc
  src/qjs_signer.cpp
  src/http_client.cpp
  src/ws_client.cpp
  src/web_defaults.cpp
  src/client.cpp
  src/event_parser.cpp
)

# Path to the bundled TikTok SDK JS files (loaded at runtime by the signer).
target_compile_definitions(ttlive PRIVATE
  TTLIVE_JS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/js")

target_include_directories(ttlive
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${TTLIVE_PROTO_OUT}
)

target_link_libraries(ttlive
  PUBLIC
    Threads::Threads
  PRIVATE
    quickjs
    curl_impersonate::curl_impersonate
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

# protobuf runtime (config target vs module variable)
if(TARGET protobuf::libprotobuf)
  target_link_libraries(ttlive PRIVATE protobuf::libprotobuf)
else()
  target_include_directories(ttlive PRIVATE ${Protobuf_INCLUDE_DIRS})
  target_link_libraries(ttlive PRIVATE ${Protobuf_LIBRARIES})
endif()

if(WIN32)
  target_link_libraries(ttlive PUBLIC ws2_32 crypt32)
  target_compile_definitions(ttlive PRIVATE _WIN32_WINNT=0x0601)
endif()

if(MSVC)
  target_compile_definitions(ttlive PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Demo executable
# ---------------------------------------------------------------------------
add_executable(ttlive_demo demo/main.cpp)
target_link_libraries(ttlive_demo PRIVATE ttlive)
