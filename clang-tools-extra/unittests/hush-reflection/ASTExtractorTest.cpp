//===-- ASTExtractorTest.cpp - Unit tests for ASTExtractor ----------------===//
//
// Uses clang::tooling::buildASTFromCodeWithArgs() to parse C++ code, then
// finds the class with [[hush::reflect]] and calls extractClassModel().
//
//===----------------------------------------------------------------------===//

#include "ASTExtractor.h"
#include "ReflectionModel.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

#include <optional>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace hush_reflection;

namespace {

std::optional<ClassModel> extractFromCode(llvm::StringRef Code) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(Code, {"-std=c++20"}, "test.hpp");
  if (!AST)
    return std::nullopt;

  auto Matcher = cxxRecordDecl(hasAttr(clang::attr::HushReflect)).bind("id");
  auto Matches = match(Matcher, AST->getASTContext());

  for (const auto &M : Matches) {
    if (const auto *Decl = M.getNodeAs<CXXRecordDecl>("id")) {
      return extractClassModel(Decl);
    }
  }
  return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. BasicClass
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, BasicClass) {
  auto Model = extractFromCode("class [[hush::reflect]] Foo {};");
  ASSERT_TRUE(Model.has_value());
  EXPECT_EQ(Model->QualifiedName, "Foo");
  EXPECT_EQ(Model->UnqualifiedName, "Foo");
  EXPECT_TRUE(Model->Fields.empty());
  EXPECT_TRUE(Model->Functions.empty());
  EXPECT_TRUE(Model->Constructors.empty());
}

// ---------------------------------------------------------------------------
// 2. NamespacedClass
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, NamespacedClass) {
  auto Model = extractFromCode(
      "namespace N { class [[hush::reflect]] Bar {}; }");
  ASSERT_TRUE(Model.has_value());
  EXPECT_EQ(Model->QualifiedName, "N::Bar");
  EXPECT_EQ(Model->UnqualifiedName, "Bar");
}

// ---------------------------------------------------------------------------
// 3. FieldExtraction
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, FieldExtraction) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::property]] int hp;
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Fields.size(), 1u);
  EXPECT_EQ(Model->Fields[0].Name, "hp");
  EXPECT_EQ(Model->Fields[0].TypeName, "int");
  EXPECT_EQ(Model->Fields[0].VisitorFieldName, "hpVisitor");
  EXPECT_FALSE(Model->Fields[0].HasCustomGetter);
}

// ---------------------------------------------------------------------------
// 4. FunctionExtraction
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, FunctionExtraction) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::function]] void act(int x);
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Functions.size(), 1u);
  EXPECT_EQ(Model->Functions[0].Name, "act");
  EXPECT_TRUE(Model->Functions[0].ReturnsVoid);
  EXPECT_EQ(Model->Functions[0].Params.size(), 1u);
}

// ---------------------------------------------------------------------------
// 5. ConstructorExtraction
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, ConstructorExtraction) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::function]] Foo(int a, float b);
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Constructors.size(), 1u);
  EXPECT_EQ(Model->Constructors[0].Params.size(), 2u);
}

// ---------------------------------------------------------------------------
// 6. SkipsStaticMethods
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, SkipsStaticMethods) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::function]] static void staticMethod();
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  EXPECT_TRUE(Model->Functions.empty());
}

// ---------------------------------------------------------------------------
// 7. SkipsUnmarkedFields
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, SkipsUnmarkedFields) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::property]] int marked;
      int unmarked;
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Fields.size(), 1u);
  EXPECT_EQ(Model->Fields[0].Name, "marked");
}

// ---------------------------------------------------------------------------
// 8. PointerParam
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, PointerParam) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::function]] void f(int *p);
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Functions.size(), 1u);
  ASSERT_EQ(Model->Functions[0].Params.size(), 1u);
  EXPECT_TRUE(Model->Functions[0].Params[0].IsPointer);
}

// ---------------------------------------------------------------------------
// 9. NonVoidReturn
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, NonVoidReturn) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::function]] int getValue();
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Functions.size(), 1u);
  EXPECT_FALSE(Model->Functions[0].ReturnsVoid);
}

// ---------------------------------------------------------------------------
// 10. MultipleMembers
// ---------------------------------------------------------------------------
TEST(ASTExtractorTest, MultipleMembers) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect]] Foo {
      [[hush::property]] int a;
      [[hush::property]] float b;
      [[hush::property]] double c;

      [[hush::function]] void f1();
      [[hush::function]] int f2();

      [[hush::function]] Foo(int x);
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  EXPECT_EQ(Model->Fields.size(), 3u);
  EXPECT_EQ(Model->Functions.size(), 2u);
  EXPECT_EQ(Model->Constructors.size(), 1u);
}
