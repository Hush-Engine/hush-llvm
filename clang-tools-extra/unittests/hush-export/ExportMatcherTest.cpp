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

// ===========================================================================
// Callback / function-pointer parameter tests
// ===========================================================================

TEST(ExportMatcherTest, FunctionWithRawCallbackParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    [[hush::export]] void OnEvent(void (*cb)(int, void *));
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "OnEvent")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);

  // The parameter must be a FuncPointer with the name embedded inside (*).
  EXPECT_EQ(Found->params[0].type.kind, CType::FuncPointer);
  EXPECT_EQ(Found->params[0].type.funcPointerDecl,
            "void (*cb)(int, void *)");
  EXPECT_EQ(Found->params[0].mode, PassMode::Reinterpret);
}

TEST(ExportMatcherTest, FunctionWithCallbackParamPreservesAliasedTypedef) {
  // Regression for the AddComponentObserverRaw bug: the inner typedef
  // must flow through as the registered C alias name (not desugared
  // to "unsigned long long").
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Entity {
      public:
        using EntityId = unsigned long long;
      };
      [[hush::export]] void OnEvent(void (*cb)(Entity::EntityId, void *));
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__OnEvent")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);

  EXPECT_EQ(Found->params[0].type.kind, CType::FuncPointer);
  // The class typedef Hush::Entity::EntityId is registered as
  // "Hush__Entity_EntityId" by processClass; that name must appear in the
  // function-pointer declaration instead of "unsigned long long".
  EXPECT_NE(Found->params[0].type.funcPointerDecl.find("Hush__Entity_EntityId"),
            std::string::npos)
      << "decl was: " << Found->params[0].type.funcPointerDecl;
  EXPECT_EQ(Found->params[0].type.funcPointerDecl.find("unsigned long long"),
            std::string::npos)
      << "decl should not contain raw 'unsigned long long': "
      << Found->params[0].type.funcPointerDecl;
}

TEST(ExportMatcherTest, FunctionWithNamespaceTypedefCallback) {
  // Reproducer for ObserverCallback_t — a namespace-scope typedef of a
  // function pointer. The matcher must lazily emit a CTypeAlias and
  // refer to the alias (Hush_ObserverCallback_t) at the use site.
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      using EventCallback_t = void (*)(int, void *);
      [[hush::export]] void Subscribe(EventCallback_t cb);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  // The alias must be in the IR's typeAliases.
  const CTypeAlias *Alias = nullptr;
  for (const auto &A : IR->typeAliases) {
    if (A.name == "Hush__EventCallback_t")
      Alias = &A;
  }
  ASSERT_NE(Alias, nullptr);
  EXPECT_NE(Alias->declaration.find("Hush__EventCallback_t"),
            std::string::npos);

  // The function parameter must use the alias name.
  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__Subscribe")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);
  EXPECT_EQ(Found->params[0].type.kind, CType::Builtin);
  EXPECT_EQ(Found->params[0].type.name, "Hush__EventCallback_t");
}

// Returning a function pointer is the symmetric counterpart to taking one
// as a parameter; resolveReturnType has the matching FP branch.
TEST(ExportMatcherTest, FunctionWithCallbackReturn) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Entity {
      public:
        using EntityId = unsigned long long;
      };
      [[hush::export]] void (*GetCallback())(Entity::EntityId, void *);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__GetCallback")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->returnType.kind, CType::FuncPointer);
  EXPECT_EQ(Found->returnMode, ReturnMode::ReinterpretPtr);
  // Inner type must be the registered alias, not the desugared form.
  EXPECT_NE(Found->returnType.funcPointerDecl.find("Hush__Entity_EntityId"),
            std::string::npos)
      << "decl was: " << Found->returnType.funcPointerDecl;
}

// Functions returning std::vector/std::span must set ReturnMode::Callback so
// the C wrapper signature gets the (data, size, userData) callback prepended.
TEST(ExportMatcherTest, FunctionReturningVectorUsesCallbackMode) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace std {
      template<class T, class A = void> class vector {
        T *data_; unsigned long size_;
      };
    }
    [[hush::export]] std::vector<int> GetItems();
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "GetItems")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->returnMode, ReturnMode::Callback);
  EXPECT_EQ(Found->callbackInnerType, "int");
}

// Function-pointer field whose inner parameter type is a class-scope
// typedef must use the registered C alias (e.g. Hush_Foo_HandleId), not
// the desugared canonical name. This is the field-side counterpart to
// FunctionWithCallbackParamPreservesAliasedTypedef.
TEST(ExportMatcherTest, FuncPointerFieldPreservesAliasedTypedef) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Entity {
      public:
        using EntityId = unsigned long long;
      };
      struct [[hush::export]] Hooks {
        void (*onCreate)(Entity::EntityId, void *);
      };
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CStruct *Found = nullptr;
  for (const auto &S : IR->structs) {
    if (S.name == "Hush__Hooks")
      Found = &S;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->fields.size(), 1u);
  EXPECT_NE(Found->fields[0].funcPointerDeclWithName.find("Hush__Entity_EntityId"),
            std::string::npos)
      << "decl was: " << Found->fields[0].funcPointerDeclWithName;
  EXPECT_EQ(Found->fields[0].funcPointerDeclWithName.find("unsigned long long"),
            std::string::npos);
}

// Member function returning a value-type with a destructor (e.g. Scene
// returning Entity by value). The wrapper must use ReturnMode::PlacementNew
// so the C++ object is move-constructed into aligned storage and the
// destructor doesn't fire on the temporary.
TEST(ExportMatcherTest, MemberFunctionReturningDestructorTypeUsesPlacementNew) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Entity {
      public:
        Entity();
        Entity(Entity &&);
        ~Entity();
      };
      class [[hush::export(Hush::Export::asHandle)]] Scene {
      public:
        [[hush::export]] Entity CreateEntity();
      };
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.cppMethodName == "CreateEntity")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_TRUE(Found->isMemberFunction);
  EXPECT_EQ(Found->returnMode, ReturnMode::PlacementNew);
  EXPECT_EQ(Found->returnType.name, "Hush__Entity");
  EXPECT_FALSE(Found->cppReturnType.empty())
      << "PlacementNew needs cppReturnType for the aligned_storage emit";
}

// const T* parameter to a registered struct should produce a Pointer CType
// with isConst on the inner type, and PassMode::Reinterpret. This is the
// "const Hush__Entity *child" pattern that's pervasive in the engine.
TEST(ExportMatcherTest, ConstPointerToStructParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Entity {};
      [[hush::export]] void Adopt(const Entity *child);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__Adopt")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);
  const auto &P = Found->params[0];
  EXPECT_EQ(P.mode, PassMode::Reinterpret);
  ASSERT_EQ(P.type.kind, CType::Pointer);
  ASSERT_NE(P.type.inner, nullptr);
  EXPECT_TRUE(P.type.inner->isConst)
      << "const-ness on the pointee was dropped";
  EXPECT_EQ(P.type.inner->name, "Hush__Entity");
}

// Pointer-to-opaque-handle is the most pervasive parameter shape in the
// generated header (every Hush__X__method takes Hush__X *self plus often
// other Hush__Y * args). Existing FunctionWithPointerParam covers int*
// only; this exercises Pointer→Struct resolution at the matcher level.
TEST(ExportMatcherTest, PointerToOpaqueHandleParam) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Scene {};
      [[hush::export]] void Render(Scene *scene);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__Render")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);
  EXPECT_EQ(Found->params[0].mode, PassMode::Reinterpret);
  ASSERT_EQ(Found->params[0].type.kind, CType::Pointer);
  ASSERT_NE(Found->params[0].type.inner, nullptr);
  EXPECT_EQ(Found->params[0].type.inner->kind, CType::OpaqueHandle);
  EXPECT_EQ(Found->params[0].type.inner->name, "Hush__Scene");
}

// Container return whose element type is a registered struct — the inner
// Vertex must resolve through the registry to the C name (VertexC), not
// stay as the raw record name. Verifies the element-resolution loop in
// the Callback return path.
TEST(ExportMatcherTest, ContainerReturnWithRegisteredElement) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace std {
      template<class T, class A = void> class vector {
        T *data_; unsigned long size_;
      };
    }
    struct [[hush::export]] Vertex { float x, y, z; };
    [[hush::export]] std::vector<Vertex> GetVertices();
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "GetVertices")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->returnMode, ReturnMode::Callback);
  EXPECT_EQ(Found->callbackInnerType, "Vertex");
}

TEST(ExportMatcherTest, StringViewReturnPreservesConstElement) {
  auto IR = runMatcher(R"cpp(
    #include <string_view>
    [[hush::export]] std::string_view GetName();
  )cpp");
  ASSERT_TRUE(IR.has_value());
  ASSERT_EQ(IR->functions.size(), 1u);
  EXPECT_EQ(IR->functions[0].returnMode, ReturnMode::Callback);
  EXPECT_EQ(IR->functions[0].callbackInnerType, "const char");
}

// Static member functions are emitted as free functions (no self pointer).
// processFunction at ExportMatcher.cpp:881 explicitly excludes static
// members from the isMemberFunction branch — untested before this.
TEST(ExportMatcherTest, StaticMemberFunctionEmittedAsFreeFunction) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Math {
      public:
        [[hush::export]] static int Add(int a, int b);
      };
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.cppName == "Hush::Math::Add")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_FALSE(Found->isMemberFunction)
      << "static member should not get a self parameter";
  EXPECT_TRUE(Found->selfCType.empty());
  ASSERT_EQ(Found->params.size(), 2u);
  EXPECT_EQ(Found->params[0].name, "a");
  EXPECT_EQ(Found->params[1].name, "b");
}

TEST(ExportMatcherTest, ForeignModuleFacadeParameters) {
  auto IR = runMatcher(R"cpp(
    #include <cstddef>
    #include <cstdint>
    #include <string_view>
    namespace Hush::Export { inline constexpr int asHandle = 0; }
    namespace Hush {
      using ModuleHandle = std::uint64_t;
      class [[hush::export(Hush::Export::asHandle)]] HushEngine {};
      class [[hush::export(Hush::Export::asHandle)]] Scene {};
      namespace Modules {
        [[hush::export]] bool Register(HushEngine *engine,
                                      ModuleHandle module,
                                      const void *descriptor);
        [[hush::export]] bool AddSystemToScene(HushEngine *engine,
                                              Scene *scene,
                                              ModuleHandle module,
                                              std::uint64_t typeId);
        [[hush::export]] bool HasMetadata(HushEngine *engine,
                                         std::uint64_t typeId,
                                         const char *keyData,
                                         std::size_t keySize);
      }
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Register = nullptr;
  const CFunction *AddSystem = nullptr;
  const CFunction *HasMetadata = nullptr;
  for (const auto &Function : IR->functions) {
    if (Function.cppName == "Hush::Modules::Register")
      Register = &Function;
    if (Function.cppName == "Hush::Modules::AddSystemToScene")
      AddSystem = &Function;
    if (Function.cppName == "Hush::Modules::HasMetadata")
      HasMetadata = &Function;
  }

  ASSERT_NE(Register, nullptr);
  ASSERT_EQ(Register->params.size(), 3u);
  EXPECT_EQ(Register->params[0].mode, PassMode::Reinterpret);
  EXPECT_EQ(Register->params[2].type.toString(), "const void *");

  ASSERT_NE(AddSystem, nullptr);
  ASSERT_EQ(AddSystem->params.size(), 4u);
  EXPECT_EQ(AddSystem->params[0].mode, PassMode::Reinterpret);
  EXPECT_EQ(AddSystem->params[1].mode, PassMode::Reinterpret);

  ASSERT_NE(HasMetadata, nullptr);
  ASSERT_EQ(HasMetadata->params.size(), 4u);
  EXPECT_FALSE(HasMetadata->params[2].isSpanParts);
  EXPECT_EQ(HasMetadata->params[2].type.toString(), "const char *");
}

// Non-const reference param to a registered struct — converts to T*
// PassMode::DerefReinterpret. The existing TypeRegistry test covers
// only the registry resolution; this exercises the full ExportMatcher
// pipeline (param decl + cast type assembly).
TEST(ExportMatcherTest, ReferenceParamToRegisteredStruct) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    namespace Hush {
      class [[hush::export(Hush::Export::asHandle)]] Mesh {};
      [[hush::export]] void Mutate(Mesh &m);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__Mutate")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  ASSERT_EQ(Found->params.size(), 1u);
  EXPECT_EQ(Found->params[0].mode, PassMode::DerefReinterpret);
  ASSERT_EQ(Found->params[0].type.kind, CType::Pointer);
  ASSERT_NE(Found->params[0].type.inner, nullptr);
  EXPECT_EQ(Found->params[0].type.inner->name, "Hush__Mesh");
  EXPECT_FALSE(Found->params[0].cppCastType.empty())
      << "DerefReinterpret needs cppCastType for the *reinterpret_cast emit";
}

// Lazy namespace-alias discovery should also handle non-function-pointer
// aliases — e.g. `using StringId = uint64_t;` at namespace scope. The
// previous test only exercised the FP path.
TEST(ExportMatcherTest, NamespaceAliasOfBuiltinIsLazyRegistered) {
  auto IR = runMatcher(R"cpp(
    namespace Hush::Export {
      inline const char* name(const char* n) { return n; }
      inline constexpr int ignore = 0;
      inline constexpr int asHandle = 0;
    }
    #include <cstdint>
    namespace Hush {
      using StringId = std::uint64_t;
      [[hush::export]] StringId Lookup(StringId id);
    }
  )cpp");
  ASSERT_TRUE(IR.has_value());

  // The alias should have been emitted into typeAliases. The underlying
  // type uses the source-level sugar (uint64_t), not the desugared form.
  const CTypeAlias *Alias = nullptr;
  for (const auto &A : IR->typeAliases) {
    if (A.name == "Hush__StringId")
      Alias = &A;
  }
  ASSERT_NE(Alias, nullptr);
  EXPECT_NE(Alias->declaration.find("uint64_t"), std::string::npos)
      << "decl was: " << Alias->declaration;
  EXPECT_NE(Alias->declaration.find("Hush__StringId"), std::string::npos);

  // Both the param and the return type should refer to the alias name.
  const CFunction *Found = nullptr;
  for (const auto &F : IR->functions) {
    if (F.name == "Hush__Lookup")
      Found = &F;
  }
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->returnType.name, "Hush__StringId");
  ASSERT_EQ(Found->params.size(), 1u);
  EXPECT_EQ(Found->params[0].type.name, "Hush__StringId");
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
