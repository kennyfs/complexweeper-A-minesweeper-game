cmake_minimum_required(VERSION 3.20)
project(complexweeper_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Pure logic library (rules + solver + undo history; no GUI, no third-party dependencies)
add_library(complexweeper_core STATIC src/game.cpp src/solver.cpp src/session.cpp)
target_include_directories(complexweeper_core PUBLIC src)
target_compile_options(complexweeper_core PRIVATE -Wall -Wextra -Wpedantic)

# C interface shared library, used by the Python (ctypes) GUI in gui/
add_library(complexweeper_capi SHARED src/capi.cpp)
target_link_libraries(complexweeper_capi PRIVATE complexweeper_core)
target_compile_options(complexweeper_capi PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(game_test tests/game_test.cpp)
target_link_libraries(game_test PRIVATE complexweeper_core)
add_test(NAME game_test COMMAND game_test)

add_executable(solver_test tests/solver_test.cpp)
target_link_libraries(solver_test PRIVATE complexweeper_core)
add_test(NAME solver_test COMMAND solver_test)
