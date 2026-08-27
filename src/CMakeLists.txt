add_library(cogito_core STATIC
  agent_loop.cpp
  audit.cpp
  budget.cpp
  canonical_json.cpp
  clock.cpp
  config.cpp
  conversation.cpp
  digest.cpp
  fsm.cpp
  ids.cpp
  invoker.cpp
  permission_gate.cpp
  permit.cpp
  policy.cpp
  secret.cpp
  secret_string.cpp
  tool.cpp
  registry.cpp
  tool_schema.cpp
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
    nlohmann_json_schema_validator::validator
    cogito_warnings
    cogito_sanitizers
)

if(BUILD_TESTING)
  target_compile_definitions(cogito_core PUBLIC COGITO_TESTING=1)
endif()

if(WIN32)
  target_link_libraries(cogito_core PRIVATE Advapi32)
endif()
