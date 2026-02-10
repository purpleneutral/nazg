cmake_minimum_required(VERSION 3.16)
project(nazg LANGUAGES CXX)

# --- Options ---------------------------------------------------------------
option(NAZG_WARN_AS_ERR "Treat warnings as errors" OFF)
option(NAZG_LTO         "Enable link-time optimization (Release only)" OFF)

# --- Tooling / standard ----------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
  message(STATUS "ccache found: ${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# --- Helper function for modules ------------------------------------------
function(add_nazg_module MODULE_NAME)
  file(GLOB_RECURSE MODULE_SOURCES "modules/${MODULE_NAME}/src/*.cpp")

  add_library(${MODULE_NAME} STATIC ${MODULE_SOURCES})

  target_include_directories(${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/${MODULE_NAME}/include>
    $<INSTALL_INTERFACE:include>
  )

  target_compile_features(${MODULE_NAME} PUBLIC cxx_std_17)

  # Link dependencies passed as additional arguments
  if (ARGC GREATER 1)
    target_link_libraries(${MODULE_NAME} PUBLIC ${ARGN})
  endif()
endfunction()

# --- Dependencies ----------------------------------------------------------
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# FTXUI for TUI
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v5.0.0
)
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
FetchContent_MakeAvailable(ftxui)

# --- Modules ---------------------------------------------------------------
add_nazg_module(blackbox)                            # First - logging foundation
add_nazg_module(config blackbox)                     # Config can log via blackbox
add_nazg_module(system blackbox)                     # System utils (uses blackbox for logging)
add_nazg_module(directive blackbox nexus system config agent)
add_nazg_module(prompt blackbox directive)           # Interactive prompts
target_link_libraries(system PUBLIC prompt)          # System uses prompt for UI
target_link_libraries(prompt PUBLIC system)          # Prompt uses system for terminal detection (circular but OK in CMake)
add_nazg_module(nexus blackbox config)               # Database persistence
target_link_libraries(nexus PUBLIC SQLite::SQLite3)
add_nazg_module(brain nexus blackbox directive)      # Intelligence (detection, snapshot, planning)
target_link_libraries(brain PUBLIC crypto)           # For SHA256 in snapshot
add_nazg_module(task system blackbox nexus brain)    # Task execution (uses brain types)
target_link_libraries(brain PUBLIC task)             # Brain can use task for builds
add_nazg_module(testsuite blackbox nexus brain system directive prompt workspace)  # Test suite (uses workspace helpers)
add_nazg_module(git nexus blackbox directive prompt system) # Git integration
target_link_libraries(git PUBLIC CURL::libcurl)
add_nazg_module(scaffold blackbox directive prompt git nexus)  # Project scaffolding (uses git)
add_nazg_module(agent blackbox nexus)
target_link_libraries(agent PUBLIC SQLite::SQLite3)
add_nazg_module(workspace blackbox nexus brain system directive)  # Workspace Time Machine
add_nazg_module(bot system blackbox directive nexus config prompt agent)    # Bot orchestration
add_nazg_module(docker_monitor blackbox nexus bot agent)                # Docker monitoring
add_nazg_module(tui blackbox directive config system nexus docker_monitor)    # TUI multiplexer
target_link_libraries(tui PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  Threads::Threads
)
add_nazg_module(engine blackbox directive system config nexus brain task testsuite scaffold git prompt bot agent workspace tui docker_monitor)
# --- App -------------------------------------------------------------------
add_executable(nazg app/main.cpp)
target_link_libraries(nazg PRIVATE engine)

add_executable(nazg-agent agent/main.cpp)
target_link_libraries(nazg-agent PRIVATE agent crypto)

enable_testing()

add_executable(nazg_tests tests/test_main.cpp)
target_link_libraries(nazg_tests PRIVATE agent bot blackbox system testsuite tui)
add_test(NAME nazg_tests COMMAND nazg_tests)

# --- Warnings --------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nazg PRIVATE -Wall -Wextra -Wpedantic)
  if (NAZG_WARN_AS_ERR)
    target_compile_options(nazg PRIVATE -Werror)
  endif()
elseif (MSVC)
  target_compile_options(nazg PRIVATE /W4)
  if (NAZG_WARN_AS_ERR)
    target_compile_options(nazg PRIVATE /WX)
  endif()
endif()

# --- LTO / IPO -------------------------------------------------------------
if (CMAKE_BUILD_TYPE STREQUAL "Release" AND NAZG_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
  if (ipo_ok)
    set_property(TARGET nazg PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "IPO/LTO not supported: ${ipo_msg}")
  endif()
endif()

# --- Install ---------------------------------------------------------------
install(TARGETS nazg RUNTIME DESTINATION bin)
