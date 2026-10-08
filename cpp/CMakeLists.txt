cmake_minimum_required(VERSION 3.20)
project(complexweeper_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 純邏輯函式庫（沒有 GUI、沒有第三方相依）
add_library(complexweeper_core src/game.cpp)
target_include_directories(complexweeper_core PUBLIC src)
target_compile_options(complexweeper_core PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(game_test tests/game_test.cpp)
target_link_libraries(game_test PRIVATE complexweeper_core)
add_test(NAME game_test COMMAND game_test)
