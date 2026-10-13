cmake_minimum_required(VERSION 3.20)
project(ControlePartida LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(../interpretacao interpretacao)

add_library(game_controller STATIC src/GameController.cpp)
target_include_directories(game_controller PUBLIC src)
target_link_libraries(game_controller PUBLIC move_interpreter)

add_executable(controller_tests tests/native_main.cpp tests/controller_tests.cpp)
target_link_libraries(controller_tests PRIVATE game_controller)
foreach(target game_controller controller_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

foreach(case startup_baseline position_gate confirmation_only duplicate_confirmation
    move_sequence captures capture_visibility castling en_passant illegal_moves
    no_move ambiguous interruption overflow promotion_choices promotion_captures
    promotion_selection promotion_observations promotion_interruption reset copy_history
    repetition fifty_move checkmate stalemate insufficient_material manual_finish
    resignation finished_immutable)
  add_test(NAME control.${case} COMMAND controller_tests ${case})
endforeach()
