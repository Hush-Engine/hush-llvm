include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

add_clang_executable(hush-reflection
  HushReflection.cpp
)

target_link_libraries(hush-reflection
  PRIVATE
  clangHushReflection
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
)
