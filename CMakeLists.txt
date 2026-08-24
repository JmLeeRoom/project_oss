cmake_minimum_required(VERSION 3.24)

project(cogito LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(COGITO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(COGITO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(COGITO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(COGITO_ENABLE_TSAN AND (COGITO_ENABLE_ASAN OR COGITO_ENABLE_UBSAN))
  message(FATAL_ERROR "TSan cannot be combined with ASan or UBSan")
endif()

add_library(cogito_warnings INTERFACE)
if(MSVC)
  target_compile_options(cogito_warnings INTERFACE /permissive- /utf-8 /W4 /WX)
else()
  target_compile_options(cogito_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
  )
endif()

add_library(cogito_sanitizers INTERFACE)
if(COGITO_ENABLE_ASAN OR COGITO_ENABLE_UBSAN OR COGITO_ENABLE_TSAN)
  if(MSVC)
    if(COGITO_ENABLE_UBSAN OR COGITO_ENABLE_TSAN)
      message(FATAL_ERROR "UBSan and TSan presets require GCC or Clang")
    endif()
    target_compile_options(cogito_sanitizers INTERFACE /fsanitize=address)
    target_link_options(cogito_sanitizers INTERFACE /fsanitize=address)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(_cogito_sanitizer_flags -fno-omit-frame-pointer)
    if(COGITO_ENABLE_ASAN)
      list(APPEND _cogito_sanitizer_flags -fsanitize=address)
    endif()
    if(COGITO_ENABLE_UBSAN)
      list(APPEND _cogito_sanitizer_flags -fsanitize=undefined)
    endif()
    if(COGITO_ENABLE_TSAN)
      list(APPEND _cogito_sanitizer_flags -fsanitize=thread)
    endif()
    target_compile_options(cogito_sanitizers INTERFACE ${_cogito_sanitizer_flags})
    target_link_options(cogito_sanitizers INTERFACE ${_cogito_sanitizer_flags})
  else()
    message(FATAL_ERROR "The selected compiler does not support the requested sanitizer lane")
  endif()
endif()

find_package(nlohmann_json 3.12.0 EXACT CONFIG REQUIRED)
find_package(OpenSSL 3.6.3 EXACT REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

add_subdirectory(src)

if(BUILD_TESTING)
  find_package(Catch2 3.15.3 EXACT CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
