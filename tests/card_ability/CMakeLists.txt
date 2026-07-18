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
# Mana Abilities Test (pure algorithms, no card database needed)
# ------------------------
add_executable(mana_abilities_test mana_abilities_test.cpp)

target_link_libraries(
  mana_abilities_test
  PRIVATE libcockatrice_card
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME mana_abilities_test COMMAND mana_abilities_test)

# ------------------------
# Activated Abilities Test (pure algorithms, no card database needed)
# ------------------------
add_executable(activated_abilities_test activated_abilities_test.cpp)

target_link_libraries(
  activated_abilities_test
  PRIVATE libcockatrice_card
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME activated_abilities_test COMMAND activated_abilities_test)

# ------------------------
# Triggered Abilities Test (pure algorithms, no card database needed)
# ------------------------
add_executable(triggered_abilities_test triggered_abilities_test.cpp)

target_link_libraries(
  triggered_abilities_test
  PRIVATE libcockatrice_card
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME triggered_abilities_test COMMAND triggered_abilities_test)

# ------------------------
# Dependencies on gtest
# ------------------------
if(NOT GTEST_FOUND)
  add_dependencies(card_keywords_test gtest)
  add_dependencies(mana_abilities_test gtest)
  add_dependencies(activated_abilities_test gtest)
  add_dependencies(triggered_abilities_test gtest)
endif()
