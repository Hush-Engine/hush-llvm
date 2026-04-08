//===-- CBindingIRTest.cpp - Unit tests for CBindingIR types --------------===//
//
// Tests that CType composition and toString() work correctly.
//
//===----------------------------------------------------------------------===//

#include "CBindingIR.h"
#include "gtest/gtest.h"

using namespace hush;

// ---------------------------------------------------------------------------
// CType::toString
// ---------------------------------------------------------------------------

TEST(CTypeTest, BuiltinToString) {
  auto T = CType::makeBuiltin("int");
  EXPECT_EQ(T.toString(), "int");
}

TEST(CTypeTest, VoidToString) {
  auto T = CType::makeVoid();
  EXPECT_EQ(T.toString(), "void");
}

TEST(CTypeTest, StructToString) {
  auto T = CType::makeStruct("Vector3");
  EXPECT_EQ(T.toString(), "Vector3");
}

TEST(CTypeTest, OpaqueHandleToString) {
  auto T = CType::makeOpaqueHandle("MyHandle");
  EXPECT_EQ(T.toString(), "MyHandle");
}

TEST(CTypeTest, EnumToString) {
  auto T = CType::makeEnum("MyEnum");
  EXPECT_EQ(T.toString(), "MyEnum");
}

TEST(CTypeTest, PointerToBuiltin) {
  auto T = CType::makePointer(CType::makeBuiltin("float"));
  EXPECT_EQ(T.toString(), "float *");
}

TEST(CTypeTest, ConstPointerToStruct) {
  auto T = CType::makePointer(CType::makeStruct("Vector3"), /*isConst=*/true);
  // The const is on the pointer itself (const ptr), not the pointee
  EXPECT_EQ(T.toString(), "Vector3 *");
  EXPECT_TRUE(T.isConst);
}

TEST(CTypeTest, PointerToPointer) {
  auto T = CType::makePointer(CType::makePointer(CType::makeBuiltin("int")));
  EXPECT_EQ(T.toString(), "int * *");
}

TEST(CTypeTest, PointerToConstBuiltin) {
  auto Inner = CType::makeBuiltin("int");
  Inner.isConst = true;
  auto T = CType::makePointer(Inner);
  EXPECT_EQ(T.toString(), "const int *");
}

TEST(CTypeTest, FuncPointerToString) {
  auto T = CType::makeFuncPointer("void (*)(int, float)");
  EXPECT_EQ(T.toString(), "void (*)(int, float)");
}

// ---------------------------------------------------------------------------
// CType::toParamDecl
// ---------------------------------------------------------------------------

TEST(CTypeTest, ParamDeclBuiltin) {
  auto T = CType::makeBuiltin("uint32_t");
  EXPECT_EQ(T.toParamDecl("count"), "uint32_t count");
}

TEST(CTypeTest, ParamDeclPointer) {
  auto T = CType::makePointer(CType::makeStruct("Vector3"));
  EXPECT_EQ(T.toParamDecl("pos"), "Vector3 * pos");
}

// ---------------------------------------------------------------------------
// CType::Kind checks
// ---------------------------------------------------------------------------

TEST(CTypeTest, KindPreserved) {
  EXPECT_EQ(CType::makeBuiltin("int").kind, CType::Builtin);
  EXPECT_EQ(CType::makeVoid().kind, CType::Void);
  EXPECT_EQ(CType::makeStruct("Foo").kind, CType::Struct);
  EXPECT_EQ(CType::makeOpaqueHandle("Bar").kind, CType::OpaqueHandle);
  EXPECT_EQ(CType::makeEnum("Baz").kind, CType::Enum);
  EXPECT_EQ(CType::makePointer(CType::makeBuiltin("int")).kind, CType::Pointer);
  EXPECT_EQ(CType::makeFuncPointer("void(*)()").kind, CType::FuncPointer);
}

TEST(CTypeTest, PointerInnerType) {
  auto T = CType::makePointer(CType::makeStruct("Mesh"));
  ASSERT_NE(T.inner, nullptr);
  EXPECT_EQ(T.inner->kind, CType::Struct);
  EXPECT_EQ(T.inner->name, "Mesh");
}
