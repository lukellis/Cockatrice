cmake_minimum_required(VERSION 3.16)
project(CardAbilityTests VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# ------------------------
# Card Keywords Test (pure algorithms, no card database needed)
# ------------------------
add_executable(card_keywords_test card_keywords_test.cpp)

target_link_libraries(
  card_keywords_test
  PRIVATE libcockatrice_card
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME card_keywords_test COMMAND card_keywords_test)

# ------------------------
# Dependencies on gtest
# ------------------------
if(NOT GTEST_FOUND)
  add_dependencies(card_keywords_test gtest)
endif()
