//===-- TypeRegistryTest.cpp - Unit tests for TypeRegistry ----------------===//
//
// Tests the TypeRegistry name-based registration and lookup, plus
// AST-based translator tests using buildASTFromCodeWithArgs().
//
//===----------------------------------------------------------------------===//

#include "TypeRegistry.h"
#include "CBindingIR.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

using namespace hush;
using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

// ===========================================================================
// Registry name-based tests (no Clang AST needed)
// ===========================================================================

TEST(TypeRegistryTest, RegisterAndLookup) {
  TypeRegistry reg;
  reg.registerType("glm::vec3", CType::makeStruct("Vector3"));

  auto res = reg.lookup("glm::vec3");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Struct);
  EXPECT_EQ(res->cType.name, "Vector3");
  EXPECT_EQ(res->cppName, "glm::vec3");
}

TEST(TypeRegistryTest, LookupUnknownReturnsNullopt) {
  TypeRegistry reg;
  EXPECT_FALSE(reg.lookup("DoesNotExist").has_value());
}

TEST(TypeRegistryTest, IsRegistered) {
  TypeRegistry reg;
  reg.registerType("Hush::Mesh", CType::makeStruct("Mesh"));

  EXPECT_TRUE(reg.isRegistered("Hush::Mesh"));
  EXPECT_FALSE(reg.isRegistered("Hush::Scene"));
}

TEST(TypeRegistryTest, RegisterEnum) {
  TypeRegistry reg;
  reg.registerType("Hush::RenderMode", CType::makeEnum("RenderMode"),
                   /*isEnum=*/true);

  auto res = reg.lookup("Hush::RenderMode");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Enum);
  EXPECT_TRUE(res->isEnum);
  EXPECT_EQ(res->enumCppType, "Hush::RenderMode");
}

TEST(TypeRegistryTest, RegisterOpaqueHandle) {
  TypeRegistry reg;
  reg.registerType("Hush::Engine", CType::makeOpaqueHandle("Engine"));

  auto res = reg.lookup("Hush::Engine");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::OpaqueHandle);
  EXPECT_EQ(res->cType.name, "Engine");
}

TEST(TypeRegistryTest, OverwriteRegistration) {
  TypeRegistry reg;
  reg.registerType("Hush::Foo", CType::makeStruct("Foo_Old"));
  reg.registerType("Hush::Foo", CType::makeStruct("Foo_New"));

  auto res = reg.lookup("Hush::Foo");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.name, "Foo_New");
}

TEST(TypeRegistryTest, RegisteredTypeNames) {
  TypeRegistry reg;
  reg.registerType("A", CType::makeStruct("A"));
  reg.registerType("B", CType::makeStruct("B"));
  reg.registerType("C", CType::makeStruct("C"));

  auto names = reg.registeredTypeNames();
  EXPECT_EQ(names.size(), 3u);
  // Map is sorted, so A < B < C
  EXPECT_EQ(names[0], "A");
  EXPECT_EQ(names[1], "B");
  EXPECT_EQ(names[2], "C");
}

// ===========================================================================
// AST-based translator tests
// ===========================================================================

namespace {

/// Helper: parse C++ code, find a variable named "target", and resolve
/// its type through the given registry.
std::optional<TypeResolution> resolveFromCode(llvm::StringRef code,
                                              const TypeRegistry &reg) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(code, {"-std=c++20"}, "test.cpp");
  if (!AST)
    return std::nullopt;

  // Find the variable declaration named "target"
  auto matcher = varDecl(hasName("target")).bind("var");
  auto matches = match(matcher, AST->getASTContext());

  for (const auto &m : matches) {
    if (const auto *VD = m.getNodeAs<VarDecl>("var")) {
      return reg.resolve(VD->getType());
    }
  }
  return std::nullopt;
}

/// Helper: same but resolves the return type of a function named "target".
std::optional<TypeResolution> resolveReturnFromCode(llvm::StringRef code,
                                                    const TypeRegistry &reg) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(code, {"-std=c++20"}, "test.cpp");
  if (!AST)
    return std::nullopt;

  auto matcher = functionDecl(hasName("target")).bind("func");
  auto matches = match(matcher, AST->getASTContext());

  for (const auto &m : matches) {
    if (const auto *FD = m.getNodeAs<FunctionDecl>("func")) {
      return reg.resolve(FD->getReturnType());
    }
  }
  return std::nullopt;
}

/// Helper: resolve a function parameter type.
std::optional<TypeResolution>
resolveParamFromCode(llvm::StringRef code, const TypeRegistry &reg,
                     unsigned paramIndex = 0) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(code, {"-std=c++20"}, "test.cpp");
  if (!AST)
    return std::nullopt;

  auto matcher = functionDecl(hasName("target")).bind("func");
  auto matches = match(matcher, AST->getASTContext());

  for (const auto &m : matches) {
    if (const auto *FD = m.getNodeAs<FunctionDecl>("func")) {
      if (paramIndex < FD->getNumParams()) {
        return reg.resolve(FD->parameters()[paramIndex]->getType());
      }
    }
  }
  return std::nullopt;
}

} // namespace

// ---- Builtin types ----

TEST(TypeTranslatorTest, BuiltinInt) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("int target = 0;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Builtin);
  EXPECT_EQ(res->cType.name, "int");
  EXPECT_FALSE(res->isEnum);
  EXPECT_FALSE(res->isContainer);
}

TEST(TypeTranslatorTest, BuiltinFloat) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("float target = 0.0f;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Builtin);
  EXPECT_EQ(res->cType.name, "float");
}

TEST(TypeTranslatorTest, BuiltinBool) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("bool target = false;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Builtin);
  EXPECT_EQ(res->cType.name, "bool");
}

// ---- Enum types ----

TEST(TypeTranslatorTest, EnumType) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode(R"cpp(
    enum Color { Red, Green, Blue };
    Color target = Red;
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Enum);
  EXPECT_TRUE(res->isEnum);
}

TEST(TypeTranslatorTest, ScopedEnumType) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode(R"cpp(
    enum class Mode : int { A, B };
    Mode target = Mode::A;
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Enum);
  EXPECT_TRUE(res->isEnum);
}

TEST(TypeTranslatorTest, RegisteredEnumUsesCustomName) {
  auto reg = createDefaultRegistry();
  reg->registerType("Mode", CType::makeEnum("HushRenderMode"), /*isEnum=*/true);

  auto res = resolveFromCode(R"cpp(
    enum class Mode : int { A, B };
    Mode target = Mode::A;
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.name, "HushRenderMode");
  EXPECT_TRUE(res->isEnum);
}

// ---- Pointer types ----

TEST(TypeTranslatorTest, PointerToBuiltin) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("int *target = nullptr;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_EQ(res->cType.inner->kind, CType::Builtin);
  EXPECT_EQ(res->cType.inner->name, "int");
}

TEST(TypeTranslatorTest, PointerToRegisteredStruct) {
  auto reg = createDefaultRegistry();
  reg->registerType("Foo", CType::makeStruct("FooC"));

  auto res = resolveFromCode(R"cpp(
    struct Foo {};
    Foo *target = nullptr;
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_EQ(res->cType.inner->kind, CType::Struct);
  EXPECT_EQ(res->cType.inner->name, "FooC");
}

TEST(TypeTranslatorTest, ConstPointerToBuiltin) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("const int *target = nullptr;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_TRUE(res->cType.inner->isConst);
  EXPECT_EQ(res->cType.inner->name, "int");
}

TEST(TypeTranslatorTest, PointerToPointer) {
  auto reg = createDefaultRegistry();
  auto res = resolveFromCode("int **target = nullptr;", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_EQ(res->cType.inner->kind, CType::Pointer);
  ASSERT_NE(res->cType.inner->inner, nullptr);
  EXPECT_EQ(res->cType.inner->inner->kind, CType::Builtin);
}

// ---- Reference types ----

TEST(TypeTranslatorTest, LValueRefToBuiltin) {
  auto reg = createDefaultRegistry();
  auto res = resolveParamFromCode("void target(int &x);", *reg);

  ASSERT_TRUE(res.has_value());
  // References are converted to pointers
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_EQ(res->cType.inner->kind, CType::Builtin);
  EXPECT_EQ(res->cType.inner->name, "int");
}

TEST(TypeTranslatorTest, ConstLValueRefToRegisteredStruct) {
  auto reg = createDefaultRegistry();
  reg->registerType("Mesh", CType::makeStruct("MeshC"));

  auto res = resolveParamFromCode(R"cpp(
    struct Mesh {};
    void target(const Mesh &m);
  )cpp",
                                  *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Pointer);
  ASSERT_NE(res->cType.inner, nullptr);
  EXPECT_EQ(res->cType.inner->kind, CType::Struct);
  EXPECT_EQ(res->cType.inner->name, "MeshC");
  EXPECT_TRUE(res->cType.inner->isConst);
}

// ---- Container types ----

TEST(TypeTranslatorTest, StdSpanInt) {
  auto reg = createDefaultRegistry();
  auto res = resolveParamFromCode(R"cpp(
    namespace std {
      template<class T, long long N = -1> class span {
        T *data_; unsigned long size_;
      };
    }
    void target(std::span<int> s);
  )cpp",
                                  *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->isContainer);
  EXPECT_EQ(res->containerElementCType.kind, CType::Builtin);
  EXPECT_EQ(res->containerElementCType.name, "int");
}

TEST(TypeTranslatorTest, StdVectorFloat) {
  auto reg = createDefaultRegistry();
  auto res = resolveParamFromCode(R"cpp(
    namespace std {
      template<class T, class A = void> class vector {
        T *data_; unsigned long size_;
      };
    }
    void target(std::vector<float> v);
  )cpp",
                                  *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->isContainer);
  EXPECT_EQ(res->containerElementCType.kind, CType::Builtin);
  EXPECT_EQ(res->containerElementCType.name, "float");
}

TEST(TypeTranslatorTest, StdStringView) {
  auto reg = createDefaultRegistry();
  auto res = resolveParamFromCode(R"cpp(
    namespace std {
      template<class CharT, class Traits = void> class basic_string_view {
        const CharT *data_; unsigned long size_;
      };
      using string_view = basic_string_view<char>;
    }
    void target(std::string_view sv);
  )cpp",
                                  *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->isContainer);
  EXPECT_EQ(res->containerElementCType.kind, CType::Builtin);
  // Element type is char
  EXPECT_EQ(res->containerElementCType.name, "char");
}

TEST(TypeTranslatorTest, ContainerOfRegisteredStruct) {
  auto reg = createDefaultRegistry();
  reg->registerType("Vertex", CType::makeStruct("VertexC"));

  auto res = resolveParamFromCode(R"cpp(
    struct Vertex {};
    namespace std {
      template<class T, long long N = -1> class span {
        T *data_; unsigned long size_;
      };
    }
    void target(std::span<Vertex> s);
  )cpp",
                                  *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->isContainer);
  EXPECT_EQ(res->containerElementCType.kind, CType::Struct);
  EXPECT_EQ(res->containerElementCType.name, "VertexC");
}

// ---- Record types ----

TEST(TypeTranslatorTest, RegisteredRecordType) {
  auto reg = createDefaultRegistry();
  reg->registerType("MyStruct", CType::makeStruct("MyStructC"));

  auto res = resolveFromCode(R"cpp(
    struct MyStruct { int x; };
    MyStruct target{};
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Struct);
  EXPECT_EQ(res->cType.name, "MyStructC");
}

TEST(TypeTranslatorTest, UnregisteredRecordReturnsNullopt) {
  auto reg = createDefaultRegistry();
  // Note: NOT registered

  auto res = resolveFromCode(R"cpp(
    struct Unknown { int x; };
    Unknown target{};
  )cpp",
                             *reg);

  // Record is not registered and no translator claims it
  // Falls through to name-based lookup which also fails
  EXPECT_FALSE(res.has_value());
}

// ---- Void return type ----

TEST(TypeTranslatorTest, VoidReturn) {
  auto reg = createDefaultRegistry();
  auto res = resolveReturnFromCode("void target();", *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Builtin);
  EXPECT_EQ(res->cType.name, "void");
}

// ---- Namespace-qualified types ----

TEST(TypeTranslatorTest, NamespacedRegisteredType) {
  auto reg = createDefaultRegistry();
  reg->registerType("N::Foo", CType::makeStruct("N_Foo"));

  auto res = resolveFromCode(R"cpp(
    namespace N { struct Foo { int x; }; }
    N::Foo target{};
  )cpp",
                             *reg);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->cType.kind, CType::Struct);
  EXPECT_EQ(res->cType.name, "N_Foo");
}

// ---- createDefaultRegistry ----

TEST(TypeRegistryTest, DefaultRegistryCreated) {
  auto reg = createDefaultRegistry();
  EXPECT_NE(reg, nullptr);
  // Should have no pre-registered types (translators handle everything)
  EXPECT_TRUE(reg->registeredTypeNames().empty());
}
