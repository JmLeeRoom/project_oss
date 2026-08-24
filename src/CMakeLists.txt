add_library(cogito_core STATIC
  canonical_json.cpp
  digest.cpp
  ids.cpp
)
add_library(cogito::core ALIAS cogito_core)

target_compile_features(cogito_core PUBLIC cxx_std_17)

target_include_directories(cogito_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(cogito_core
  PUBLIC
    nlohmann_json::nlohmann_json
  PRIVATE
    OpenSSL::Crypto
    Threads::Threads
    cogito_warnings
    cogito_sanitizers
)
