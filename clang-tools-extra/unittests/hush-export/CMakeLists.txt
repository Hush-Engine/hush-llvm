set(LLVM_LINK_COMPONENTS Support)

get_filename_component(HUSH_EXPORT_SOURCE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/../../hush-export REALPATH)
include_directories(${HUSH_EXPORT_SOURCE_DIR})

add_extra_unittest(HushExportTests
  CBindingIRTest.cpp
  CodeEmitterTest.cpp
  TypeRegistryTest.cpp
  ExportMatcherTest.cpp
)

clang_target_link_libraries(HushExportTests
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
)

target_link_libraries(HushExportTests
  PRIVATE
  clangHushExport
)
