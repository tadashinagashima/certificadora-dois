cmake_minimum_required(VERSION 3.20)
project(TabuleiroChess LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(thc_backend STATIC src/thc/thc.cpp)
add_library(chess_adapter STATIC src/ChessAdapter.cpp)
target_include_directories(chess_adapter PUBLIC src)
target_link_libraries(chess_adapter PUBLIC thc_backend)

add_executable(chess_adapter_tests tests/native_main.cpp tests/adapter_tests.cpp)
target_link_libraries(chess_adapter_tests PRIVATE chess_adapter)
if(MSVC)
  target_compile_options(chess_adapter PRIVATE /W4 /WX)
  target_compile_options(chess_adapter_tests PRIVATE /W4 /WX)
else()
  target_compile_options(chess_adapter PRIVATE -Wall -Wextra -Werror)
  target_compile_options(chess_adapter_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
foreach(case backend_internals initial_coordinates legal_moves invalid_moves
    queries_preserve apply_reset fen_loading capture castling castling_constraints
    en_passant promotions san terminal material fifty_move repetition copy_history)
  add_test(NAME ${case} COMMAND chess_adapter_tests ${case})
endforeach()
