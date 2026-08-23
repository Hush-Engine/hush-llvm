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

  auto Matcher = cxxRecordDecl(anyOf(hasAttr(clang::attr::HushReflect),
                                     hasAttr(clang::attr::HushSystem)))
                     .bind("id");
  auto Matches = match(Matcher, AST->getASTContext());

  for (const auto &M : Matches) {
    if (const auto *Decl = M.getNodeAs<CXXRecordDecl>("id")) {
      return extractClassModel(Decl, AST->getDiagnostics());
    }
  }
  return std::nullopt;
}

bool extractionReportsError(llvm::StringRef Code) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(Code, {"-std=c++20"}, "test.hpp");
  if (!AST)
    return true;
  AST->getDiagnostics().setClient(new IgnoringDiagConsumer(), true);

  auto Matcher = cxxRecordDecl(anyOf(hasAttr(clang::attr::HushReflect),
                                     hasAttr(clang::attr::HushSystem)))
                     .bind("id");
  auto Matches = match(Matcher, AST->getASTContext());
  for (const auto &M : Matches) {
    if (const auto *Decl = M.getNodeAs<CXXRecordDecl>("id")) {
      extractClassModel(Decl, AST->getDiagnostics());
      break;
    }
  }
  return AST->getDiagnostics().hasErrorOccurred();
}

std::optional<ModuleInitFunction>
extractModuleInitFromCode(llvm::StringRef Code) {
  std::unique_ptr<ASTUnit> AST =
      buildASTFromCodeWithArgs(Code, {"-std=c++20"}, "test.hpp");
  if (!AST)
    return std::nullopt;

  auto Matches = match(functionDecl(hasAttr(clang::attr::HushModuleInit))
                           .bind("module_init"),
                       AST->getASTContext());
  for (const auto &M : Matches) {
    if (const auto *Decl =
            M.getNodeAs<FunctionDecl>("module_init"))
      return extractModuleInitFunction(Decl, AST->getDiagnostics());
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
  EXPECT_EQ(Model->CanonicalName, "Foo");
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
  EXPECT_EQ(Model->CanonicalName, "N.Bar");
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

TEST(ASTExtractorTest, SystemConfigurationAndMetadata) {
  auto Model = extractFromCode(R"cpp(
    namespace Hush {
      class Scene {};
      class ISystem { public: explicit ISystem(Scene &) {} };
      namespace Reflection { constexpr void order(unsigned) {} }
    }
    class [[hush::system(Hush::Reflection::order(17))]] GameSystem
        : public Hush::ISystem {
    public:
      explicit GameSystem(Hush::Scene &scene) : ISystem(scene) {}
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  EXPECT_TRUE(Model->IsSystem);
  EXPECT_EQ(Model->SystemOrder, 17u);
  EXPECT_EQ(Model->CanonicalName, "GameSystem");
}

TEST(ASTExtractorTest, ExtractsMetadata) {
  auto Model = extractFromCode(R"cpp(
    class [[hush::reflect, hush::meta("category", "gameplay")]] Foo {
      [[hush::property, hush::meta("range", "0,100")]] int hp;
      [[hush::function, hush::meta("action", "true")]] void Heal();
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Metadata.size(), 1u);
  EXPECT_EQ(Model->Metadata[0].Key, "category");
  ASSERT_EQ(Model->Fields.size(), 1u);
  EXPECT_EQ(Model->Fields[0].Metadata[0].Value, "0,100");
  ASSERT_EQ(Model->Functions.size(), 1u);
  EXPECT_EQ(Model->Functions[0].Metadata[0].Key, "action");
}

TEST(ASTExtractorTest, CanonicalNameOverride) {
  auto Model = extractFromCode(R"cpp(
    namespace Hush::Reflection {
      template <unsigned N> constexpr void name(const char (&)[N]) {}
    }
    namespace Internal {
      class [[hush::reflect(Hush::Reflection::name("Game.Player"))]] Player {};
    }
  )cpp");
  ASSERT_TRUE(Model.has_value());
  EXPECT_EQ(Model->CanonicalName, "Game.Player");
}

TEST(ASTExtractorTest, ModuleInitFunction) {
  auto Model = extractModuleInitFromCode(R"cpp(
    namespace Game { [[hush::module_init]] void Initialize() {} }
  )cpp");
  ASSERT_TRUE(Model.has_value());
  EXPECT_EQ(Model->QualifiedName, "Game::Initialize");
}

TEST(ASTExtractorTest, ExtractsCustomPropertyAccessors) {
  auto Model = extractFromCode(R"cpp(
    namespace Hush::Reflection {
      template <typename T> constexpr void Getter(T) {}
      template <typename T> constexpr void Setter(T) {}
    }
    class [[hush::reflect]] Player {
    public:
      int GetHealth() const;
      void SetHealth(int);
      [[hush::property(Hush::Reflection::Getter(&Player::GetHealth),
                       Hush::Reflection::Setter(&Player::SetHealth))]]
      int health;
    };
  )cpp");
  ASSERT_TRUE(Model.has_value());
  ASSERT_EQ(Model->Fields.size(), 1u);
  EXPECT_TRUE(Model->Fields[0].HasCustomGetter);
  EXPECT_EQ(Model->Fields[0].GetterName, "GetHealth");
  EXPECT_TRUE(Model->Fields[0].HasCustomSetter);
  EXPECT_EQ(Model->Fields[0].SetterName, "SetHealth");
}

TEST(ASTExtractorTest, RejectsSystemConstructorWithRequiredExtraArgument) {
  EXPECT_TRUE(extractionReportsError(R"cpp(
    namespace Hush {
      class Scene {};
      class ISystem { public: explicit ISystem(Scene &) {} };
    }
    class [[hush::system]] InvalidSystem : public Hush::ISystem {
    public:
      InvalidSystem(Hush::Scene &scene, int required) : ISystem(scene) {}
    };
  )cpp"));
}

TEST(ASTExtractorTest, AcceptsSystemConstructorWithDefaultedExtraArgument) {
  EXPECT_FALSE(extractionReportsError(R"cpp(
    namespace Hush {
      class Scene {};
      class ISystem { public: explicit ISystem(Scene &) {} };
    }
    class [[hush::system]] ValidSystem : public Hush::ISystem {
    public:
      ValidSystem(Hush::Scene &scene, int optional = 0) : ISystem(scene) {}
    };
  )cpp"));
}

TEST(ASTExtractorTest, RejectsSystemOrderBeforeNarrowingConversion) {
  EXPECT_TRUE(extractionReportsError(R"cpp(
    namespace Hush {
      class Scene {};
      class ISystem { public: explicit ISystem(Scene &) {} };
      namespace Reflection { constexpr void order(unsigned short) {} }
    }
    class [[hush::system(Hush::Reflection::order(65536))]] InvalidSystem
        : public Hush::ISystem {
    public:
      explicit InvalidSystem(Hush::Scene &scene) : ISystem(scene) {}
    };
  )cpp"));
}

TEST(ASTExtractorTest, RejectsAmbiguousSystemConstructors) {
  EXPECT_TRUE(extractionReportsError(R"cpp(
    namespace Hush {
      class Scene {};
      class ISystem { public: explicit ISystem(Scene &) {} };
    }
    class [[hush::system]] InvalidSystem : public Hush::ISystem {
    public:
      explicit InvalidSystem(Hush::Scene &scene) : ISystem(scene) {}
      InvalidSystem(Hush::Scene &scene, int optional = 0) : ISystem(scene) {}
    };
  )cpp"));
}

TEST(ASTExtractorTest, RejectsReservedMetadataKeys) {
  EXPECT_TRUE(extractionReportsError(R"cpp(
    class [[hush::reflect, hush::meta("hush.system", "true")]] Foo {};
  )cpp"));
}
