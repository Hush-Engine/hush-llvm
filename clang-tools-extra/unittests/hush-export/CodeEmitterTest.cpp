//===-- CodeEmitterTest.cpp - Unit tests for CodeEmitter ------------------===//
//
// Pure unit tests — no AST dependency.
// Constructs CBindingIR structs manually, calls CodeEmitter::emit(),
// and checks for expected substrings in the output.
//
//===----------------------------------------------------------------------===//

#include "CodeEmitter.h"
#include "CBindingIR.h"

#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace hush;

namespace {

struct EmitResult {
  std::string Header;
  std::string Impl;
};

static EmitResult emitIR(const CBindingIR &IR) {
  EmitResult Result;
  llvm::raw_string_ostream HeaderOS(Result.Header);
  llvm::raw_string_ostream ImplOS(Result.Impl);
  CodeEmitter Emitter;
  Emitter.emit(IR, HeaderOS, ImplOS);
  HeaderOS.flush();
  ImplOS.flush();
  return Result;
}

// ===========================================================================
// Enum tests
// ===========================================================================

TEST(CodeEmitterTest, PlainEnum) {
  CBindingIR IR;
  CEnumDef Enum;
  Enum.name = "Color";
  Enum.isPlainEnum = true;
  Enum.values = {{"Red", 0}, {"Green", 1}, {"Blue", 2}};
  IR.enums.push_back(Enum);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("typedef enum Color {"));
  EXPECT_TRUE(H.contains("Color_Red = 0,"));
  EXPECT_TRUE(H.contains("Color_Green = 1,"));
  EXPECT_TRUE(H.contains("Color_Blue = 2,"));
  EXPECT_TRUE(H.contains("} Color;"));
}

TEST(CodeEmitterTest, ScopedEnumAsTypedef) {
  CBindingIR IR;
  CEnumDef Enum;
  Enum.name = "RenderFlags";
  Enum.isPlainEnum = false;
  Enum.underlyingType = "uint32_t";
  Enum.values = {{"None", 0}, {"Wireframe", 1}, {"Textured", 2}};
  IR.enums.push_back(Enum);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("typedef uint32_t RenderFlags;"));
  EXPECT_TRUE(H.contains("#define RenderFlags_None 0"));
  EXPECT_TRUE(H.contains("#define RenderFlags_Wireframe 1"));
  EXPECT_TRUE(H.contains("#define RenderFlags_Textured 2"));
  // Should NOT contain typedef enum
  EXPECT_FALSE(H.contains("typedef enum"));
}

// ===========================================================================
// Struct tests
// ===========================================================================

TEST(CodeEmitterTest, OpaqueHandleStruct) {
  CBindingIR IR;
  CStruct S;
  S.name = "Engine";
  S.cppName = "Hush::Engine";
  S.isOpaque = true;
  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("typedef struct Engine Engine;"));
  // Should not have opening brace (no fields exposed)
  EXPECT_FALSE(H.contains("typedef struct Engine {"));
}

TEST(CodeEmitterTest, TransparentStructWithFields) {
  CBindingIR IR;
  CStruct S;
  S.name = "Vector3";
  S.cppName = "glm::vec3";

  CField FX;
  FX.name = "x";
  FX.type = CType::makeBuiltin("float");
  S.fields.push_back(FX);

  CField FY;
  FY.name = "y";
  FY.type = CType::makeBuiltin("float");
  S.fields.push_back(FY);

  CField FZ;
  FZ.name = "z";
  FZ.type = CType::makeBuiltin("float");
  S.fields.push_back(FZ);

  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("typedef struct Vector3 {"));
  EXPECT_TRUE(H.contains("float x;"));
  EXPECT_TRUE(H.contains("float y;"));
  EXPECT_TRUE(H.contains("float z;"));
  EXPECT_TRUE(H.contains("} Vector3;"));
}

TEST(CodeEmitterTest, StructWithOpaqueField) {
  CBindingIR IR;
  CStruct S;
  S.name = "MyClass";
  S.cppName = "Hush::MyClass";

  CField PublicField;
  PublicField.name = "value";
  PublicField.type = CType::makeBuiltin("int");
  S.fields.push_back(PublicField);

  CField PrivateField;
  PrivateField.name = "m_member0";
  PrivateField.isOpaque = true;
  PrivateField.opaqueSize = 8;
  PrivateField.opaqueAlign = 8;
  S.fields.push_back(PrivateField);

  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("int value;"));
  EXPECT_TRUE(H.contains("alignas(8) char m_member0[8];"));
}

TEST(CodeEmitterTest, StructWithFuncPointerField) {
  CBindingIR IR;
  CStruct S;
  S.name = "Callbacks";
  S.cppName = "Hush::Callbacks";

  CField FP;
  FP.name = "onClick";
  FP.type = CType::makeFuncPointer("void (*)(int)");
  FP.funcPointerDeclWithName = "void (*onClick)(int)";
  S.fields.push_back(FP);

  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("void (*onClick)(int);"));
}

// ===========================================================================
// Destructor tests
// ===========================================================================

TEST(CodeEmitterTest, DestructorGenerated) {
  CBindingIR IR;
  CStruct S;
  S.name = "Mesh";
  S.cppName = "Hush::Rendering::Mesh";
  S.cppUnqualifiedName = "Mesh";
  S.needsDestructor = true;

  CField F;
  F.name = "vertexCount";
  F.type = CType::makeBuiltin("uint32_t");
  S.fields.push_back(F);

  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  // Header should declare the destructor
  EXPECT_TRUE(H.contains("void Mesh_destroy(Mesh **self);"));

  // Impl should have the destructor body
  EXPECT_TRUE(I.contains("void Mesh_destroy(Mesh **self)"));
  EXPECT_TRUE(I.contains("Hush::Rendering::Mesh *selfClass = reinterpret_cast<"
                          "Hush::Rendering::Mesh *>(*self);"));
  EXPECT_TRUE(I.contains("selfClass->~Mesh();"));
  EXPECT_TRUE(I.contains("*self = nullptr;"));
}

TEST(CodeEmitterTest, NoDestructorWhenNotNeeded) {
  CBindingIR IR;
  CStruct S;
  S.name = "Vector3";
  S.cppName = "glm::vec3";
  S.needsDestructor = false;

  IR.structs.push_back(S);

  auto [Header, Impl] = emitIR(IR);

  EXPECT_EQ(std::string::npos, Header.find("_destroy"));
  EXPECT_EQ(std::string::npos, Impl.find("_destroy"));
}

// ===========================================================================
// Free function tests
// ===========================================================================

TEST(CodeEmitterTest, SimpleFreeFunction) {
  CBindingIR IR;
  CFunction F;
  F.name = "Hush_GetVersion";
  F.cppName = "Hush::GetVersion";
  F.returnType = CType::makeBuiltin("int");
  F.returnMode = ReturnMode::Direct;

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(H.contains("extern int Hush_GetVersion(void);"));
  EXPECT_TRUE(I.contains("int Hush_GetVersion(void)"));
  EXPECT_TRUE(I.contains("auto result______ = Hush::GetVersion()"));
  EXPECT_TRUE(I.contains("return result______;"));
}

TEST(CodeEmitterTest, VoidFreeFunction) {
  CBindingIR IR;
  CFunction F;
  F.name = "Hush_Init";
  F.cppName = "Hush::Init";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("void Hush_Init(void)"));
  // Should not have "auto result______ ="
  EXPECT_FALSE(I.contains("result______"));
}

TEST(CodeEmitterTest, FunctionWithBuiltinParams) {
  CBindingIR IR;
  CFunction F;
  F.name = "Hush_Add";
  F.cppName = "Hush::Add";
  F.returnType = CType::makeBuiltin("int");
  F.returnMode = ReturnMode::Direct;

  CParam A;
  A.name = "a";
  A.type = CType::makeBuiltin("int");
  A.mode = PassMode::Direct;
  F.params.push_back(A);

  CParam B;
  B.name = "b";
  B.type = CType::makeBuiltin("int");
  B.mode = PassMode::Direct;
  F.params.push_back(B);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(H.contains("extern int Hush_Add(int a, int b);"));
  EXPECT_TRUE(I.contains("Hush::Add(a, b)"));
}

// ===========================================================================
// Member function tests
// ===========================================================================

TEST(CodeEmitterTest, MemberFunction) {
  CBindingIR IR;
  CFunction F;
  F.name = "Scene_load";
  F.cppName = "Hush::Scene::load";
  F.cppMethodName = "load";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;
  F.isMemberFunction = true;
  F.selfCType = "Scene";
  F.selfCppType = "Hush::Scene";

  CParam Path;
  Path.name = "path";
  Path.type = CType::makePointer(CType::makeBuiltin("char"));
  Path.mode = PassMode::Direct;
  F.params.push_back(Path);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(H.contains("Scene *self, char * path"));
  EXPECT_TRUE(I.contains("auto selfClass = reinterpret_cast<Hush::Scene*>(self);"));
  EXPECT_TRUE(I.contains("selfClass->load(path)"));
}

// ===========================================================================
// PassMode tests
// ===========================================================================

TEST(CodeEmitterTest, EnumParam_StaticCast) {
  CBindingIR IR;
  CFunction F;
  F.name = "SetMode";
  F.cppName = "Hush::SetMode";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;

  CParam P;
  P.name = "mode";
  P.type = CType::makeEnum("RenderMode");
  P.mode = PassMode::StaticCastEnum;
  P.cppCastType = "Hush::RenderMode";
  F.params.push_back(P);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("static_cast<Hush::RenderMode>(mode)"));
}

TEST(CodeEmitterTest, PointerParam_Reinterpret) {
  CBindingIR IR;
  CFunction F;
  F.name = "ProcessMesh";
  F.cppName = "Hush::ProcessMesh";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;

  CParam P;
  P.name = "mesh";
  P.type = CType::makePointer(CType::makeStruct("Mesh"));
  P.mode = PassMode::Reinterpret;
  P.cppCastType = "Hush::Mesh*";
  F.params.push_back(P);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("reinterpret_cast<Hush::Mesh*>(mesh)"));
}

TEST(CodeEmitterTest, ReferenceParam_DerefReinterpret) {
  CBindingIR IR;
  CFunction F;
  F.name = "UpdateScene";
  F.cppName = "Hush::UpdateScene";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;

  CParam P;
  P.name = "scene";
  P.type = CType::makePointer(CType::makeStruct("Scene"));
  P.mode = PassMode::DerefReinterpret;
  P.cppCastType = "Hush::Scene*";
  F.params.push_back(P);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("*reinterpret_cast<Hush::Scene*>(scene)"));
}

TEST(CodeEmitterTest, SpanParam) {
  CBindingIR IR;
  CFunction F;
  F.name = "SetData";
  F.cppName = "Hush::SetData";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Void;

  CParam P;
  P.name = "buffer";
  P.type = CType::makeBuiltin("int");
  P.mode = PassMode::SpanFromParts;
  P.isSpanParts = true;
  P.cppContainerType = "std::span<int>";
  P.cppInnerRealType = "int";
  F.params.push_back(P);

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  // Should expand to two params in the signature
  EXPECT_TRUE(H.contains("int *bufferData, const size_t bufferSize"));

  // Should construct the span in the body
  EXPECT_TRUE(I.contains("auto bufferData__ = std::span<int>(reinterpret_cast<int*>(bufferData), bufferSize)"));
  // Should pass the constructed span to the call
  EXPECT_TRUE(I.contains("Hush::SetData(bufferData__)"));
}

// ===========================================================================
// ReturnMode tests
// ===========================================================================

TEST(CodeEmitterTest, ReturnCallback) {
  CBindingIR IR;
  CFunction F;
  F.name = "GetItems";
  F.cppName = "Hush::GetItems";
  F.returnType = CType::makeVoid();
  F.returnMode = ReturnMode::Callback;
  F.callbackInnerType = "int";

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  // Signature should have callback params
  EXPECT_TRUE(H.contains("void (*retFunc)(int*, size_t, void*)"));
  EXPECT_TRUE(H.contains("void* retUserData"));

  // Body should invoke the callback
  EXPECT_TRUE(I.contains("retFunc(reinterpret_cast<int*>(result______.data()), "
                          "result______.size(), retUserData)"));
}

TEST(CodeEmitterTest, ReturnPlacementNew) {
  CBindingIR IR;
  CFunction F;
  F.name = "CreateMesh";
  F.cppName = "Hush::CreateMesh";
  F.returnType = CType::makeStruct("Mesh");
  F.returnMode = ReturnMode::PlacementNew;
  F.cppReturnType = "Hush::Mesh";

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("std::aligned_storage_t<sizeof(Hush::Mesh)>"));
  EXPECT_TRUE(I.contains("new (resultPtr)"));
  EXPECT_TRUE(I.contains("std::move(result______)"));
}

TEST(CodeEmitterTest, ReturnReinterpretPtr) {
  CBindingIR IR;
  CFunction F;
  F.name = "GetEngine";
  F.cppName = "Hush::GetEngine";
  F.returnType = CType::makePointer(CType::makeStruct("Engine"));
  F.returnMode = ReturnMode::ReinterpretPtr;

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("return reinterpret_cast<Engine *>(result______)"));
}

TEST(CodeEmitterTest, ReturnStaticCastEnum) {
  CBindingIR IR;
  CFunction F;
  F.name = "GetMode";
  F.cppName = "Hush::GetMode";
  F.returnType = CType::makeEnum("RenderMode");
  F.returnMode = ReturnMode::StaticCastEnum;

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("return static_cast<RenderMode>(result______)"));
}

TEST(CodeEmitterTest, ReturnDerefReinterpret) {
  CBindingIR IR;
  CFunction F;
  F.name = "GetPos";
  F.cppName = "Hush::GetPos";
  F.returnType = CType::makeStruct("Vector3");
  F.returnMode = ReturnMode::DerefReinterpret;

  IR.functions.push_back(F);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("return *reinterpret_cast<Vector3*>(&result______)"));
}

// ===========================================================================
// Function pointer table tests
// ===========================================================================

TEST(CodeEmitterTest, FuncPtrTable) {
  CBindingIR IR;

  CFunction F1;
  F1.name = "Hush_Init";
  F1.cppName = "Hush::Init";
  F1.returnType = CType::makeVoid();
  F1.returnMode = ReturnMode::Void;
  IR.functions.push_back(F1);

  CFunction F2;
  F2.name = "Hush_GetVersion";
  F2.cppName = "Hush::GetVersion";
  F2.returnType = CType::makeBuiltin("int");
  F2.returnMode = ReturnMode::Direct;
  IR.functions.push_back(F2);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(H.contains("typedef struct HushFuncPtrTable {"));
  EXPECT_TRUE(H.contains("HushFuncPtr_Hush_Init"));
  EXPECT_TRUE(H.contains("HushFuncPtr_Hush_GetVersion"));
  EXPECT_TRUE(H.contains("} HushFuncPtrTable;"));
  EXPECT_TRUE(H.contains("#ifdef HUSH_STATIC_BINDING"));
  EXPECT_TRUE(H.contains("extern HushFuncPtrTable HUSH_FUNCPTR_TABLE;"));

  EXPECT_TRUE(I.contains("Hush_Init,"));
  EXPECT_TRUE(I.contains("Hush_GetVersion,"));
}

// ===========================================================================
// Header preamble / postamble
// ===========================================================================

TEST(CodeEmitterTest, HeaderGuards) {
  CBindingIR IR;
  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);

  EXPECT_TRUE(H.contains("#pragma once"));
  EXPECT_TRUE(H.contains("#include <stdint.h>"));
  EXPECT_TRUE(H.contains("#ifdef __cplusplus"));
  EXPECT_TRUE(H.contains("extern \"C\" {"));
  EXPECT_TRUE(H.contains("#endif"));
}

TEST(CodeEmitterTest, ImplPreamble) {
  CBindingIR IR;
  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef I(Impl);

  EXPECT_TRUE(I.contains("// Auto-generated file"));
  EXPECT_TRUE(I.contains("// DO NOT EDIT"));
  EXPECT_TRUE(I.contains("#include \"bindings.hpp\""));
  EXPECT_TRUE(I.contains("#include \"HushBindings.h\""));
}

// ===========================================================================
// End-to-end: full IR with enums + structs + functions
// ===========================================================================

TEST(CodeEmitterTest, FullIREndToEnd) {
  CBindingIR IR;

  // An enum
  CEnumDef Enum;
  Enum.name = "Hush_ShapeType";
  Enum.isPlainEnum = true;
  Enum.values = {{"Circle", 0}, {"Square", 1}};
  IR.enums.push_back(Enum);

  // A transparent struct
  CStruct Vec2;
  Vec2.name = "Vector2";
  Vec2.cppName = "glm::vec2";
  CField VX;
  VX.name = "x";
  VX.type = CType::makeBuiltin("float");
  Vec2.fields.push_back(VX);
  CField VY;
  VY.name = "y";
  VY.type = CType::makeBuiltin("float");
  Vec2.fields.push_back(VY);
  IR.structs.push_back(Vec2);

  // An opaque handle
  CStruct Engine;
  Engine.name = "Engine";
  Engine.cppName = "Hush::Engine";
  Engine.isOpaque = true;
  IR.structs.push_back(Engine);

  // A member function
  CFunction GetPos;
  GetPos.name = "Engine_getPosition";
  GetPos.cppName = "Hush::Engine::getPosition";
  GetPos.cppMethodName = "getPosition";
  GetPos.returnType = CType::makeStruct("Vector2");
  GetPos.returnMode = ReturnMode::DerefReinterpret;
  GetPos.isMemberFunction = true;
  GetPos.selfCType = "Engine";
  GetPos.selfCppType = "Hush::Engine";
  IR.functions.push_back(GetPos);

  auto [Header, Impl] = emitIR(IR);
  llvm::StringRef H(Header);
  llvm::StringRef I(Impl);

  // Verify ordering: enums, then structs, then functions
  size_t EnumPos = Header.find("typedef enum Hush_ShapeType");
  size_t StructPos = Header.find("typedef struct Vector2");
  size_t HandlePos = Header.find("typedef struct Engine Engine");
  size_t FuncPos = Header.find("extern Vector2 Engine_getPosition");

  ASSERT_NE(EnumPos, std::string::npos);
  ASSERT_NE(StructPos, std::string::npos);
  ASSERT_NE(HandlePos, std::string::npos);
  ASSERT_NE(FuncPos, std::string::npos);

  EXPECT_LT(EnumPos, StructPos);
  EXPECT_LT(StructPos, HandlePos);
  EXPECT_LT(HandlePos, FuncPos);
}

} // namespace
