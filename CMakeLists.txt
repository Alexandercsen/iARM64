cmake_minimum_required(VERSION 3.20)
project(iARM64 C)

set(CMAKE_C_STANDARD 11)


set(CMAKE_PREFIX_PATH "/opt/homebrew")


set(SDL2_DIR "/opt/homebrew/lib/cmake/SDL2")
set(SDL2_TTF_DIR "/opt/homebrew/lib/cmake/SDL2_ttf")

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

add_executable(iARM64 main.c)

target_include_directories(iARM64 PRIVATE
        /opt/homebrew/include
)

target_link_libraries(iARM64
        SDL2::SDL2
        SDL2_ttf::SDL2_ttf
)