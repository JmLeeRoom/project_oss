set(COGITO_CORE_SOURCES
  canonical_json.cpp
  clock.cpp
  config.cpp
  digest.cpp
  fsm.cpp
  ids.cpp
  secret.cpp
  secret_string.cpp
  tool.cpp
  registry.cpp
  tool_schema.cpp
)

function(cogito_configure_core target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_17)

  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  )

  target_link_libraries(${target_name}
    PUBLIC
      nlohmann_json::nlohmann_json
    PRIVATE
      OpenSSL::Crypto
      Threads::Threads
      nlohmann_json_schema_validator::validator
      cogito_warnings
      cogito_sanitizers
  )

  if(WIN32)
    target_link_libraries(${target_name} PRIVATE Advapi32)
  endif()
endfunction()

add_library(cogito_core STATIC ${COGITO_CORE_SOURCES})
add_library(cogito::core ALIAS cogito_core)
cogito_configure_core(cogito_core)

if(BUILD_TESTING)
  add_library(cogito_core_testing STATIC ${COGITO_CORE_SOURCES})
  target_compile_definitions(cogito_core_testing PRIVATE COGITO_TESTING=1)
  cogito_configure_core(cogito_core_testing)
endif()
