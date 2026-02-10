# Test module
file(GLOB_RECURSE TEST_SOURCES "src/*.cpp")

add_library(test STATIC ${TEST_SOURCES})

target_include_directories(test PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/test/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/config/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/git/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/prompt/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(test PUBLIC cxx_std_17)

# Link dependencies
target_link_libraries(test PUBLIC
  blackbox
  nexus
  brain
  system
  directive
  git
  prompt
)
