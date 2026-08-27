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

function(cogito_add_ticket_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target}
    PRIVATE
      cogito::core
      Catch2::Catch2WithMain
      cogito_warnings
      cogito_sanitizers
  )
  catch_discover_tests(${target} ${ARGN})
endfunction()

cogito_add_ticket_test(config_test config_test.cpp)
cogito_add_ticket_test(secret_test secret_test.cpp)
cogito_add_ticket_test(fsm_test fsm_test.cpp TEST_PREFIX "fsm_test: ")
cogito_add_ticket_test(identity_test identity_test.cpp TEST_PREFIX "identity_test: ")
cogito_add_ticket_test(permit_test permit_test.cpp TEST_PREFIX "permit_test: ")
cogito_add_ticket_test(policy_test policy_test.cpp TEST_PREFIX "policy_test: ")
cogito_add_ticket_test(budget_test budget_test.cpp TEST_PREFIX "budget_test: ")
cogito_add_ticket_test(gate_test gate_test.cpp TEST_PREFIX "gate_test: ")
cogito_add_ticket_test(audit_test audit_test.cpp TEST_PREFIX "audit_test: ")
cogito_add_ticket_test(conversation_test conversation_test.cpp TEST_PREFIX "conversation_test: ")
cogito_add_ticket_test(invoker_test invoker_test.cpp TEST_PREFIX "invoker_test: ")
cogito_add_ticket_test(loop_test loop_test.cpp TEST_PREFIX "loop_test: ")
