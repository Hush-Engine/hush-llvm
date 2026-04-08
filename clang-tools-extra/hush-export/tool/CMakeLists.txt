include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

add_clang_executable(hush-export
  HushExport.cpp
)

target_link_libraries(hush-export
  PRIVATE
  clangHushExport
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
)
