set(LLVM_LINK_COMPONENTS support)

add_clang_executable(hush-reflection
        HushReflection.cpp
        HushReflectionMatcher.cpp
        ClassField.cpp
        ReflectedClass.cpp
)
target_link_libraries(hush-reflection
        PRIVATE
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
)