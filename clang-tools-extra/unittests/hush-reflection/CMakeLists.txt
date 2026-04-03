set(LLVM_LINK_COMPONENTS Support)

get_filename_component(HUSH_REFLECTION_SOURCE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/../../hush-reflection REALPATH)
include_directories(${HUSH_REFLECTION_SOURCE_DIR})

add_extra_unittest(HushReflectionTests
  ReflectionEmitterTest.cpp
  ASTExtractorTest.cpp
  HushReflectionMatcherTest.cpp
)

clang_target_link_libraries(HushReflectionTests
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
)

target_link_libraries(HushReflectionTests
  PRIVATE
  clangHushReflection
)
