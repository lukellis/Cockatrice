cmake_minimum_required(VERSION 3.16)
project(RulesTests VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# ------------------------
# Rules Engine Test (pure decision logic + priority state machine, no server/DB needed)
# ------------------------
add_executable(rules_engine_test rules_engine_test.cpp)

target_link_libraries(
  rules_engine_test
  PRIVATE libcockatrice_rules
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME rules_engine_test COMMAND rules_engine_test)

# ------------------------
# Dependencies on gtest
# ------------------------
if(NOT GTEST_FOUND)
  add_dependencies(rules_engine_test gtest)
endif()
