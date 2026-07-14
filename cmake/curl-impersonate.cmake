# Downloads a prebuilt libcurl-impersonate for the current platform and exposes
# an imported target ``curl_impersonate::curl_impersonate``.
#
# curl-impersonate is a BoringSSL-based fork of libcurl that reproduces Chrome's
# exact TLS/HTTP2/header fingerprints (the same lib curl_cffi wraps). TikTok's
# WAF checks these fingerprints, so this transport is required to connect.
#
# Prebuilt libraries come from:
#   https://github.com/lexiforest/curl-impersonate/releases
#
# Override with -DCURL_IMPERSONATE_LOCAL_DIR=/path/to/extracted (a dir that
# contains include/ and the libcurl-impersonate.* files) to use a local copy.

include(FetchContent)

set(CURL_IMPERSONATE_VERSION "v2.0.0a5" CACHE STRING "curl-impersonate release tag")

# ---------------------------------------------------------------------------
# Resolve the release asset name for this platform.
# ---------------------------------------------------------------------------
set(_ci_asset "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_ci_asset "libcurl-impersonate-${CURL_IMPERSONATE_VERSION}.aarch64-linux-gnu.tar.gz")
  else()
    set(_ci_asset "libcurl-impersonate-${CURL_IMPERSONATE_VERSION}.x86_64-linux-gnu.tar.gz")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_ci_asset "libcurl-impersonate-${CURL_IMPERSONATE_VERSION}.arm64-macos.tar.gz")
  else()
    set(_ci_asset "libcurl-impersonate-${CURL_IMPERSONATE_VERSION}.x86_64-macos.tar.gz")
  endif()
elseif(WIN32)
  set(_ci_asset "libcurl-impersonate-${CURL_IMPERSONATE_VERSION}.x86_64-win32.tar.gz")
endif()

if(DEFINED CURL_IMPERSONATE_LOCAL_DIR)
  set(_ci_root "${CURL_IMPERSONATE_LOCAL_DIR}")
  message(STATUS "Using local curl-impersonate at ${_ci_root}")
else()
  if(_ci_asset STREQUAL "")
    message(FATAL_ERROR "No curl-impersonate prebuilt asset for this platform. "
                        "Set -DCURL_IMPERSONATE_LOCAL_DIR to a local build.")
  endif()
  set(_ci_url "https://github.com/lexiforest/curl-impersonate/releases/download/${CURL_IMPERSONATE_VERSION}/${_ci_asset}")
  message(STATUS "Fetching curl-impersonate: ${_ci_url}")
  FetchContent_Declare(curl_impersonate_dl URL "${_ci_url}")
  FetchContent_MakeAvailable(curl_impersonate_dl)
  set(_ci_root "${curl_impersonate_dl_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Locate headers + library within the extracted tree.
# ---------------------------------------------------------------------------
find_path(CI_INCLUDE_DIR
  NAMES curl/curl.h
  PATHS "${_ci_root}/include" "${_ci_root}"
  NO_DEFAULT_PATH REQUIRED)

# Prefer the STATIC lib. The prebuilt shared .so is built with zig and bundles
# its own libunwind, whose exported _Unwind_* symbols interpose on the process
# and clash with libstdc++'s exception machinery -> "terminate called
# recursively" when a C++ exception unwinds. Linking the static archive into
# our binary keeps our toolchain's unwinder authoritative.
find_library(CI_STATIC
  NAMES libcurl-impersonate.a libcurl-impersonate-chrome.a
  PATHS "${_ci_root}" "${_ci_root}/lib"
  NO_DEFAULT_PATH)
find_library(CI_SHARED
  NAMES curl-impersonate curl-impersonate-chrome
  PATHS "${_ci_root}" "${_ci_root}/lib"
  NO_DEFAULT_PATH)

# GLOBAL: parent projects link this too (e.g. DearTT's model downloader).
add_library(curl_impersonate::curl_impersonate UNKNOWN IMPORTED GLOBAL)
if(CI_STATIC)
  set_target_properties(curl_impersonate::curl_impersonate PROPERTIES
    IMPORTED_LOCATION "${CI_STATIC}")
  message(STATUS "curl-impersonate (static): ${CI_STATIC}")
  # A static BoringSSL/brotli/zlib build pulls in C++/math/compression symbols.
  find_package(Threads REQUIRED)
  find_package(ZLIB REQUIRED)
  # The C++ runtime name differs by platform: libc++ on macOS (there is no
  # libstdc++), libstdc++ elsewhere.
  if(APPLE)
    set(_ci_cxxrt c++)
  else()
    set(_ci_cxxrt stdc++)
  endif()
  set(_ci_extra ${_ci_cxxrt} m Threads::Threads ZLIB::ZLIB)
  # brotli / zstd / idn2 if present on the system (curl-impersonate deps).
  foreach(_dep brotlienc brotlidec brotlicommon zstd idn2)
    find_library(_ci_lib_${_dep} ${_dep})
    if(_ci_lib_${_dep})
      list(APPEND _ci_extra ${_ci_lib_${_dep}})
    endif()
  endforeach()
  # macOS: the static archive references system symbols not otherwise pulled in:
  #   SystemConfiguration -> proxy discovery (_SCDynamicStoreCopyProxies)
  #   libiconv            -> _iconv* (charset conversion in the IDN path)
  #   icucore             -> _uidna_* (IDNA/punycode via system ICU)
  #   CoreFoundation      -> transitive dep of SystemConfiguration
  if(APPLE)
    find_library(_ci_iconv iconv)
    if(_ci_iconv)
      list(APPEND _ci_extra ${_ci_iconv})
    endif()
    find_library(_ci_icucore icucore)
    if(_ci_icucore)
      list(APPEND _ci_extra ${_ci_icucore})
    endif()
    list(APPEND _ci_extra
      "-framework SystemConfiguration"
      "-framework CoreFoundation")
  endif()
  target_link_libraries(curl_impersonate::curl_impersonate INTERFACE ${_ci_extra})
elseif(CI_SHARED)
  set_target_properties(curl_impersonate::curl_impersonate PROPERTIES
    IMPORTED_LOCATION "${CI_SHARED}")
  message(WARNING "curl-impersonate: only the shared lib was found; if you hit "
                  "'terminate called recursively', use the static archive.")
else()
  message(FATAL_ERROR "libcurl-impersonate not found under ${_ci_root}")
endif()

set_target_properties(curl_impersonate::curl_impersonate PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CI_INCLUDE_DIR}")

set(CURL_IMPERSONATE_ROOT "${_ci_root}" CACHE PATH "" FORCE)
