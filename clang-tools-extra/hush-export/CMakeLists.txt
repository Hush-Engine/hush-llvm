set(LLVM_LINK_COMPONENTS support)

add_clang_library(clangHushExport STATIC
        ParserCommon.cpp
        CodeEmitter.cpp
        TypeRegistry.cpp
        ExportMatcher.cpp
)

clang_target_link_libraries(clangHushExport
        PRIVATE
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
)

add_subdirectory(tool)
