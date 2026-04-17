cmake_minimum_required(VERSION 3.20)

project(Roundtable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_MOLD "Use the mold linker when supported" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

include(CTest)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)

set(SANITIZER_FLAGS "")

if(ENABLE_ASAN)
  list(APPEND SANITIZER_FLAGS -fsanitize=address)
endif()

if(ENABLE_UBSAN)
  list(APPEND SANITIZER_FLAGS -fsanitize=undefined)
endif()

if(SANITIZER_FLAGS)
  add_compile_options(${SANITIZER_FLAGS} -fno-omit-frame-pointer)
  add_link_options(${SANITIZER_FLAGS} -fno-omit-frame-pointer)
endif()

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

list(REMOVE_ITEM SOURCE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/dap_probe_main.cpp"
)

add_executable(roundtable  ${SOURCE_FILES}
)

add_executable(roundtable_dap_probe
  src/dap_probe_main.cpp
  src/dap_session.cpp
  src/debug_session.cpp
  src/memory_view.cpp
)

add_executable(roundtable_dap_sample
  samples/dap_sample_target.cpp
)

target_include_directories(roundtable PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(roundtable_dap_probe PRIVATE
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

target_link_libraries(roundtable_dap_probe
)

if(BUILD_TESTING)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2
    GIT_TAG v3.8.1
  )

  FetchContent_MakeAvailable(Catch2)

  add_executable(roundtable_tests
    tests/dap_session_tests.cpp
    tests/debug_session_tests.cpp
    tests/pane_rows_tests.cpp
    tests/pane_state_tests.cpp
    tests/memory_view_tests.cpp
    src/dap_session.cpp
    src/debug_session.cpp
    src/memory_view.cpp
    src/pane_rows.cpp
    src/pane_state.cpp
  )

  target_include_directories(roundtable_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(roundtable_tests
    PRIVATE Catch2::Catch2WithMain
    PRIVATE ftxui::component
  )

  include(Catch)
  catch_discover_tests(roundtable_tests)
endif()
