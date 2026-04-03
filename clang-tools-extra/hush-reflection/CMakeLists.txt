set(LLVM_LINK_COMPONENTS support)

add_clang_library(clangHushReflection STATIC
  HushReflectionMatcher.cpp
  ASTExtractor.cpp
  ReflectionEmitter.cpp
)

clang_target_link_libraries(clangHushReflection
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
)

add_subdirectory(tool)
