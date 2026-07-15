cmake_minimum_required(VERSION 3.16)
project(CommanderTests VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# ------------------------
# Definitions
# ------------------------
add_definitions("-DCARDDB_DATADIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/\"")

# ------------------------
# Commander Rules Test (pure algorithms, no card database needed)
# ------------------------
add_executable(commander_rules_test commander_rules_test.cpp)

target_link_libraries(
  commander_rules_test
  PRIVATE libcockatrice_card
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME commander_rules_test COMMAND commander_rules_test)

# ------------------------
# Commander Deck Validator Test (integration: CardDatabase + DeckListModel)
# ------------------------
add_executable(commander_deck_validator_test ${VERSION_STRING_CPP} commander_deck_validator_test.cpp)

target_link_libraries(
  commander_deck_validator_test
  PRIVATE libcockatrice_card
  PRIVATE libcockatrice_models
  PRIVATE Threads::Threads
  PRIVATE ${GTEST_BOTH_LIBRARIES}
  PRIVATE ${TEST_QT_MODULES}
)

add_test(NAME commander_deck_validator_test COMMAND commander_deck_validator_test)

# ------------------------
# Dependencies on gtest
# ------------------------
if(NOT GTEST_FOUND)
  add_dependencies(commander_rules_test gtest)
  add_dependencies(commander_deck_validator_test gtest)
endif()
