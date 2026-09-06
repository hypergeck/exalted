cmake_minimum_required(VERSION 3.21)
project(multimadness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable, header-only core: input events, ring buffer, key state, routing,
# dispatch planning, layout, geometry. Builds and tests on any platform.
add_library(mm_portable INTERFACE)
target_include_directories(mm_portable INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /wd4324)  # C4324: padding from alignas(64) is intentional
else()
  add_compile_options(-Wall -Wextra)
endif()

enable_testing()
file(GLOB MM_TESTS CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(src ${MM_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE mm_portable)
  target_include_directories(${name} PRIVATE tests)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE Threads::Threads)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# Windows-only: Win32 adapters and the mm-core CLI.
if(WIN32)
  add_library(mm_win STATIC
    core/src/win/capture.cpp
    core/src/win/inject.cpp
    core/src/win/focus.cpp
    core/src/win/launcher.cpp
    core/src/win/discovery.cpp
    core/src/win/window.cpp
    core/src/win/broadcaster.cpp)
  target_link_libraries(mm_win PUBLIC mm_portable user32 dwmapi)
  target_compile_definitions(mm_win PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)

  add_executable(mm-core core/tools/mm_core.cpp)
  target_link_libraries(mm-core PRIVATE mm_win)
endif()
