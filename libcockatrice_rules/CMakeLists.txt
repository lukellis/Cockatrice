set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(HEADERS libcockatrice/rules/rules_engine.h)

if(Qt6_FOUND)
  qt6_wrap_cpp(MOC_SOURCES ${HEADERS})
elseif(Qt5_FOUND)
  qt5_wrap_cpp(MOC_SOURCES ${HEADERS})
endif()

add_library(
  libcockatrice_rules STATIC
  ${MOC_SOURCES} libcockatrice/rules/rules_engine.cpp libcockatrice/rules/rules_engine.h
  libcockatrice/rules/commander_rules.cpp libcockatrice/rules/commander_rules.h
  libcockatrice/rules/commander_counter_names.h
)

target_include_directories(libcockatrice_rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  libcockatrice_rules
  PUBLIC libcockatrice_utility
  PUBLIC libcockatrice_card
  PUBLIC ${QT_CORE_MODULE}
)
