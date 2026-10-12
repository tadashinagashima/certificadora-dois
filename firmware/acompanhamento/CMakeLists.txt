cmake_minimum_required(VERSION 3.20)
project(AcompanhamentoJogada LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(move_tracker STATIC src/MoveTracker.cpp)
target_include_directories(move_tracker PUBLIC src)

add_executable(move_tracker_tests tests/test_move_tracker.cpp)
target_link_libraries(move_tracker_tests PRIVATE move_tracker)

foreach(target move_tracker move_tracker_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

enable_testing()
set(cases
  no_baseline initialization unchanged_frames quiet_move replacement
  capture_attacker_first capture_victim_first capture_grouped
  castling_kingside castling_queenside en_passant promotion
  grouped_changes all_64_squares discard_and_correction snapshot_independence
  capacity_and_overflow interruption interruption_before_baseline
  discard_during_interruption combined_losses timestamp_wrap
)
foreach(case IN LISTS cases)
  add_test(NAME ${case} COMMAND move_tracker_tests ${case})
endforeach()
