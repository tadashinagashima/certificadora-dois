cmake_minimum_required(VERSION 3.20)
project(RegistroPartida LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(../controle controle)

add_library(game_record STATIC src/GameRecord.cpp)
target_include_directories(game_record PUBLIC src)
target_link_libraries(game_record PUBLIC game_controller)

add_executable(record_tests tests/native_main.cpp tests/record_tests.cpp)
target_link_libraries(record_tests PRIVATE game_record)
foreach(target game_record record_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

foreach(case empty_start ordinary_pipeline ignored_confirmations rejected_pipeline
    promotion_pipeline fools_mate scholars_mate castling en_passant promotion_choices
    promotion_captures san_disambiguation custom_numbering context_guard invalid_moves
    finish_results terminal_positions finish_validation finish_idempotence reset metadata
    metadata_invalid date_validation wrapping capacity history_preservation)
  add_test(NAME record.${case} COMMAND record_tests ${case})
endforeach()
