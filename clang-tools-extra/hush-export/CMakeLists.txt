set(LLVM_LINK_COMPONENTS support)

add_clang_executable(hush-export
        HushExport.cpp
        FunctionParser.cpp
        DefParser.cpp
        ParserCommon.cpp
)
target_link_libraries(hush-export
        PRIVATE
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
)