//===-- ExportMatcherTest.cpp - Unit tests for ExportMatcher --------------===//
//
// Uses buildASTFromCodeWithArgs() + MatchFinder to test the full pipeline:
// C++ source → ExportMatcher → CBindingIR.
//
//===----------------------------------------------------------------------===//

#include "ExportMatcher.h"
#include "CBindingIR.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

using namespace hush;
using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

namespace {

/// Run ExportMatcher on the given C++ code and return the resulting IR.
/// The code should use [[hush::export(...)]] attributes.
std::optional<CBindingIR> runMatcher(llvm::StringRef Code) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(Code, {"-std=c++20"}, "test.hpp");
  if (!AST)
    return std::nullopt;

  ExportMatcher Matcher;
  MatchFinder Finder;

  auto EnumM = decl(enumDecl(hasAttr(attr::HushExport))).bind("hushExportable");
  auto ClassM =
      decl(recordDecl(hasAttr(attr::HushExport))).bind("hushExportable");
  auto FuncM =
      decl(functionDecl(hasAttr(attr::HushExport))).bind("hushExportable");

  Finder.addMatcher(EnumM, &Matcher);
  Finder.addMatcher(ClassM, &Matcher);
  Finder.addMatcher(FuncM, &Matcher);

  Finder.matchAST(AST->getASTContext());
  return Matcher.takeIR();
}

} // namespace

// ===========================================================================
// Glm pre-registration
// ===========================================================================

TEST(ExportMatcherTest, GlmTypesPreregistered) {
  auto IR = runMatcher("// empty file");
  ASSERT_TRUE(IR.has_value());

  // Should have glm structs pre-registered
  bool foundVec3 = false;
  for (const auto &S : IR->structs) {
    if (S.name == "Vector3" && S.cppName == "glm::vec3") {
      foundVec3 = true;
      EXPECT_EQ(S.fields.size(), 3u);
    }
  }
  EXPECT_TRUE(foundVec3);
}

// ===========================================================================
// Enum tests
// ===========================================================================

TEST(ExportMatcherTest, PlainEnum) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    enum [[hush::export]] Color { Red, Green, Blue };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  // Find the Color enum (skip glm stuff)
  const CEnumDef *found = nullptr;
  for (const auto &E : IR->enums) {
    if (E.name == "Color")
      found = &E;
  }
  ASSERT_NE(found, nullptr);
  EXPECT_TRUE(found->isPlainEnum);
  ASSERT_EQ(found->values.size(), 3u);
  EXPECT_EQ(found->values[0].name, "Red");
  EXPECT_EQ(found->values[0].value, 0);
}

TEST(ExportMatcherTest, ScopedEnum) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    enum class [[hush::export]] Flags : unsigned int { None = 0, Active = 1 };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CEnumDef *found = nullptr;
  for (const auto &E : IR->enums) {
    if (E.name == "Flags")
      found = &E;
  }
  ASSERT_NE(found, nullptr);
  EXPECT_FALSE(found->isPlainEnum);
  EXPECT_EQ(found->underlyingType, "unsigned int");
  ASSERT_EQ(found->values.size(), 2u);
}

// ===========================================================================
// Class tests
// ===========================================================================

TEST(ExportMatcherTest, OpaqueHandle) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    class [[hush::export(Hush::Export::asHandle)]] Engine {
      int internal;
    };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CStruct *found = nullptr;
  for (const auto &S : IR->structs) {
    if (S.cppName == "Engine")
      found = &S;
  }
  ASSERT_NE(found, nullptr);
  EXPECT_TRUE(found->isOpaque);
  EXPECT_EQ(found->name, "Engine");
}

TEST(ExportMatcherTest, TransparentStruct) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    struct [[hush::export]] MyData {
      int x;
      float y;
    };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CStruct *found = nullptr;
  for (const auto &S : IR->structs) {
    if (S.cppName == "MyData")
      found = &S;
  }
  ASSERT_NE(found, nullptr);
  EXPECT_FALSE(found->isOpaque);
  ASSERT_EQ(found->fields.size(), 2u);
  EXPECT_EQ(found->fields[0].name, "x");
  EXPECT_EQ(found->fields[1].name, "y");
}

TEST(ExportMatcherTest, PrivateFieldsAreOpaque) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    class [[hush::export]] Widget {
    public:
      int visible;
    private:
      int secret;
    };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CStruct *found = nullptr;
  for (const auto &S : IR->structs) {
    if (S.cppName == "Widget")
      found = &S;
  }
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->fields.size(), 2u);
  EXPECT_FALSE(found->fields[0].isOpaque);
  EXPECT_EQ(found->fields[0].name, "visible");
  EXPECT_TRUE(found->fields[1].isOpaque);
}

// ===========================================================================
// Function tests
// ===========================================================================

TEST(ExportMatcherTest, SimpleVoidFunction) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    [[hush::export]] void Init();
  )cpp");
  ASSERT_TRUE(IR.has_value());

  ASSERT_EQ(IR->functions.size(), 1u);
  auto &F = IR->functions[0];
  EXPECT_EQ(F.name, "Init");
  EXPECT_EQ(F.cppName, "Init");
  EXPECT_EQ(F.returnMode, ReturnMode::Void);
  EXPECT_FALSE(F.isMemberFunction);
  EXPECT_TRUE(F.params.empty());
}

// Note: Hush::Export::name("...") attribute parsing is tested via the real
// tool against the engine codebase. The CallExpr in the attribute requires
// full Sema resolution that buildASTFromCodeWithArgs doesn't provide reliably.

TEST(ExportMatcherTest, FunctionWithBuiltinParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    [[hush::export]] int Add(int a, int b);
  )cpp");
  ASSERT_TRUE(IR.has_value());

  ASSERT_EQ(IR->functions.size(), 1u);
  auto &F = IR->functions[0];
  ASSERT_EQ(F.params.size(), 2u);
  EXPECT_EQ(F.params[0].name, "a");
  EXPECT_EQ(F.params[0].mode, PassMode::Direct);
  EXPECT_EQ(F.params[1].name, "b");
}

TEST(ExportMatcherTest, FunctionWithPointerParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    [[hush::export]] void Process(int *data);
  )cpp");
  ASSERT_TRUE(IR.has_value());

  ASSERT_EQ(IR->functions.size(), 1u);
  ASSERT_EQ(IR->functions[0].params.size(), 1u);
  EXPECT_EQ(IR->functions[0].params[0].mode, PassMode::Reinterpret);
}

TEST(ExportMatcherTest, FunctionWithEnumParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    enum [[hush::export]] Mode { A, B };
    [[hush::export]] void SetMode(Mode m);
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "SetMode")
      found = &F;
  }
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->params.size(), 1u);
  EXPECT_EQ(found->params[0].mode, PassMode::StaticCastEnum);
}

TEST(ExportMatcherTest, MemberFunction) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    class [[hush::export(Hush::Export::asHandle)]] Scene {
    public:
      [[hush::export]] void load(int id);
    };
  )cpp");
  ASSERT_TRUE(IR.has_value());

  ASSERT_EQ(IR->functions.size(), 1u);
  auto &F = IR->functions[0];
  EXPECT_TRUE(F.isMemberFunction);
  EXPECT_EQ(F.selfCType, "Scene");
  EXPECT_EQ(F.selfCppType, "Scene");
  EXPECT_EQ(F.cppMethodName, "load");
  ASSERT_EQ(F.params.size(), 1u);
  EXPECT_EQ(F.params[0].name, "id");
}

TEST(ExportMatcherTest, IgnoredFunction) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    [[hush::export(Hush::Export::ignore)]] void Secret();
    [[hush::export]] void Public();
  )cpp");
  ASSERT_TRUE(IR.has_value());

  // Only the non-ignored function should be present
  ASSERT_EQ(IR->functions.size(), 1u);
  EXPECT_EQ(IR->functions[0].name, "Public");
}
