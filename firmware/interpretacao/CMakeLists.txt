cmake_minimum_required(VERSION 3.20)
project(InterpretacaoJogada LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(../acompanhamento acompanhamento)
add_subdirectory(../xadrez xadrez)

add_library(move_interpreter STATIC src/MoveInterpreter.cpp)
target_include_directories(move_interpreter PUBLIC src)
target_link_libraries(move_interpreter PUBLIC move_tracker chess_adapter)

add_executable(interpreter_tests tests/native_main.cpp tests/interpreter_tests.cpp)
target_link_libraries(interpreter_tests PRIVATE move_interpreter)
foreach(target move_interpreter interpreter_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

foreach(case ordinary_frames square_limits capture_orders capture_visibility
    capture_ambiguity intermediate_placements castling_orders castling_constraints
    en_passant_orders en_passant_constraints promotion_choices promotion_captures
    promotion_ambiguity selection_constraints no_move unreliable_input
    malformed_history position_mismatch history_capacity discard_stale_frames
    timestamps preserves_inputs legal_constraints move_sequence)
  add_test(NAME interpretation.${case} COMMAND interpreter_tests ${case})
endforeach()
