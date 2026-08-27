add_executable(cogito_tests
  canonical_json_test.cpp
  clock_test.cpp
  config_test.cpp
  digest_test.cpp
  fsm_test.cpp
  ids_test.cpp
  secret_test.cpp
  secret_string_test.cpp
  tool_test.cpp
  registry_test.cpp
  tool_schema_test.cpp
)

target_compile_definitions(cogito_tests PRIVATE COGITO_TESTING=1)

target_link_libraries(cogito_tests
  PRIVATE
    cogito_core_testing
    Catch2::Catch2WithMain
    cogito_warnings
    cogito_sanitizers
)

include(Catch)
catch_discover_tests(cogito_tests)
