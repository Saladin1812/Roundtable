cmake_minimum_required(VERSION 3.20)

project(Roundtable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_MOLD "Use the mold linker when supported" ON)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)

if(USE_MOLD)
  find_program(MOLD_EXECUTABLE mold)

  if(MOLD_EXECUTABLE)
    add_link_options(-fuse-ld=mold)
  else()
    message(WARNING "USE_MOLD is ON, but 'mold' was not found in PATH")
  endif()
endif()

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(roundtable  ${SOURCE_FILES}
)

target_include_directories(roundtable PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v6.1.9
)

FetchContent_MakeAvailable(ftxui)

target_link_libraries(roundtable
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
)
