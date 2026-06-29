# Downloads prebuilt slangc for the current platform.
# Sets SLANG_COMPILER and SLANG_INCLUDE_DIR after inclusion.

set(SLANG_VERSION "2026.12" CACHE STRING "Slang release version")

# Detect platform + arch
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_SLANG_PLATFORM "macos-aarch64")
    else()
        set(_SLANG_PLATFORM "macos-x86_64")
    endif()
    set(_SLANG_EXT "tar.gz")
elseif(WIN32)
    set(_SLANG_PLATFORM "windows-x86_64")
    set(_SLANG_EXT "zip")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_SLANG_PLATFORM "linux-aarch64")
    else()
        set(_SLANG_PLATFORM "linux-x86_64")
    endif()
    set(_SLANG_EXT "tar.gz")
endif()

set(_SLANG_URL
    "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-${_SLANG_PLATFORM}.${_SLANG_EXT}"
)

include(FetchContent)
FetchContent_Declare(
    slang_prebuilt
    URL "${_SLANG_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(slang_prebuilt)

# slangc binary
if(WIN32)
    set(SLANG_COMPILER "${slang_prebuilt_SOURCE_DIR}/bin/slangc.exe" CACHE FILEPATH "slangc executable")
else()
    set(SLANG_COMPILER "${slang_prebuilt_SOURCE_DIR}/bin/slangc" CACHE FILEPATH "slangc executable")
endif()

# Headers for runtime Slang API (hot-reload in DS_DEV)
set(SLANG_INCLUDE_DIR "${slang_prebuilt_SOURCE_DIR}/include" CACHE PATH "Slang include directory")

if(NOT EXISTS "${SLANG_COMPILER}")
    message(FATAL_ERROR "slangc not found at ${SLANG_COMPILER}. Check SLANG_VERSION.")
endif()

message(STATUS "Slang ${SLANG_VERSION}: ${SLANG_COMPILER}")
