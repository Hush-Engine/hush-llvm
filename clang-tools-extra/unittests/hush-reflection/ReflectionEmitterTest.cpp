//===-- ReflectionEmitterTest.cpp - Unit tests for ReflectionEmitter ------===//
//
// Pure unit tests — no AST dependency.
// Constructs ClassModel / FieldModel / etc. structs manually, calls
// ReflectionEmitter::emit() into an llvm::raw_string_ostream, and checks
// for expected substrings.
//
//===----------------------------------------------------------------------===//

#include "ReflectionEmitter.h"
#include "ReflectionModel.h"

#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace hush_reflection;

namespace {

static std::string emitToString(const ClassModel &Model) {
  std::string Output;
  llvm::raw_string_ostream OS(Output);
  ReflectionEmitter Emitter;
  Emitter.emit(Model, OS);
  OS.flush();
  return Output;
}

// ---------------------------------------------------------------------------
// 1. EmptyClass
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, EmptyClass) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Empty";
  Model.UnqualifiedName = "Empty";

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("TypeName()"));
  EXPECT_TRUE(S.contains("\"Empty\""));
  EXPECT_TRUE(S.contains("TypeId()"));
  EXPECT_TRUE(S.contains("RegisterReflection"));
  EXPECT_TRUE(S.contains(".Register()"));
  EXPECT_TRUE(S.contains("Serialize"));
  EXPECT_TRUE(S.contains("Deserialize"));
  EXPECT_TRUE(S.contains("private:"));
}

// ---------------------------------------------------------------------------
// 2. SingleFieldDefaultAccessors
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, SingleFieldDefaultAccessors) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Player";
  Model.UnqualifiedName = "Player";

  FieldModel Field;
  Field.Name = "health";
  Field.TypeName = "float";
  Field.ParentClassName = "Hush::Player";
  Field.VisitorFieldName = "healthVisitor";
  Field.HasCustomGetter = false;
  Field.HasCustomSetter = false;
  Model.Fields.push_back(Field);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains(".AddProperty("));
  EXPECT_TRUE(S.contains("\"health\""));
  EXPECT_TRUE(S.contains("offsetof(Hush::Player, health)"));
  // Default setter writes to field directly
  EXPECT_TRUE(S.contains("instance->health = *value.value()"));
  // Default getter reads field directly
  EXPECT_TRUE(S.contains("instance->health)"));
}

// ---------------------------------------------------------------------------
// 3. FieldWithCustomGetter
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FieldWithCustomGetter) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Player";
  Model.UnqualifiedName = "Player";

  FieldModel Field;
  Field.Name = "pos";
  Field.TypeName = "float";
  Field.ParentClassName = "Hush::Player";
  Field.VisitorFieldName = "posVisitor";
  Field.HasCustomGetter = true;
  Field.GetterName = "getPos";
  Field.HasCustomSetter = false;
  Model.Fields.push_back(Field);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("instance->getPos()"));
}

// ---------------------------------------------------------------------------
// 4. FieldWithCustomSetter
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FieldWithCustomSetter) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Player";
  Model.UnqualifiedName = "Player";

  FieldModel Field;
  Field.Name = "pos";
  Field.TypeName = "float";
  Field.ParentClassName = "Hush::Player";
  Field.VisitorFieldName = "posVisitor";
  Field.HasCustomGetter = false;
  Field.HasCustomSetter = true;
  Field.SetterName = "setPos";
  Model.Fields.push_back(Field);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("instance->setPos(value.value())"));
}

// ---------------------------------------------------------------------------
// 5. FieldWithBothCustomAccessors
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FieldWithBothCustomAccessors) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Player";
  Model.UnqualifiedName = "Player";

  FieldModel Field;
  Field.Name = "pos";
  Field.TypeName = "float";
  Field.ParentClassName = "Hush::Player";
  Field.VisitorFieldName = "posVisitor";
  Field.HasCustomGetter = true;
  Field.GetterName = "getPos";
  Field.HasCustomSetter = true;
  Field.SetterName = "setPos";
  Model.Fields.push_back(Field);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("instance->getPos()"));
  EXPECT_TRUE(S.contains("instance->setPos(value.value())"));
}

// ---------------------------------------------------------------------------
// 6. ConstructorZeroParams
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, ConstructorZeroParams) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  ConstructorModel Ctor;
  Ctor.ParentClassName = "Hush::Foo";
  Model.Constructors.push_back(Ctor);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("AddConstructor"));
  EXPECT_TRUE(S.contains("args.size() != 0"));
  EXPECT_TRUE(S.contains("CreateInPlace<Hush::Foo>()"));
  EXPECT_TRUE(S.contains("AddInPlaceConstructor"));
  EXPECT_TRUE(S.contains("std::construct_at"));
}

// ---------------------------------------------------------------------------
// 7. ConstructorOneParam
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, ConstructorOneParam) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  ConstructorModel Ctor;
  Ctor.ParentClassName = "Hush::Foo";

  ParamModel Param;
  Param.TypeName = "const int &";
  Param.CanonicalTypeName = "int";
  Param.IsPointer = false;
  Ctor.Params.push_back(Param);
  Model.Constructors.push_back(Ctor);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("args.size() != 1"));
  EXPECT_TRUE(S.contains("args[0].Get<int>"));
  EXPECT_TRUE(S.contains("*param0Result.value()"));
}

// ---------------------------------------------------------------------------
// 8. ConstructorPointerParam
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, ConstructorPointerParam) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  ConstructorModel Ctor;
  Ctor.ParentClassName = "Hush::Foo";

  ParamModel Param;
  Param.TypeName = "int *";
  Param.CanonicalTypeName = "int *";
  Param.IsPointer = true;
  Ctor.Params.push_back(Param);
  Model.Constructors.push_back(Ctor);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  // For pointer params the CreateInPlace call uses value() without dereference
  EXPECT_TRUE(S.contains("param0Result.value()"));
  // The dereference '*' should NOT appear immediately before param0Result
  // in the CreateInPlace argument (pointer path omits the '*')
  EXPECT_TRUE(S.contains("CreateInPlace<Hush::Foo>(param0Result.value())"));
}

// ---------------------------------------------------------------------------
// 9. ConstructorMultipleParams
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, ConstructorMultipleParams) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  ConstructorModel Ctor;
  Ctor.ParentClassName = "Hush::Foo";

  for (int I = 0; I < 3; ++I) {
    ParamModel Param;
    Param.TypeName = "int";
    Param.CanonicalTypeName = "int";
    Param.IsPointer = false;
    Ctor.Params.push_back(Param);
  }
  Model.Constructors.push_back(Ctor);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("args.size() != 3"));
}

// ---------------------------------------------------------------------------
// 10. FunctionVoidReturn
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FunctionVoidReturn) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  FunctionModel Func;
  Func.Name = "doStuff";
  Func.ParentClassName = "Hush::Foo";
  Func.ReturnsVoid = true;
  Model.Functions.push_back(Func);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("AddFunction"));
  EXPECT_TRUE(S.contains("instance->"));
  EXPECT_TRUE(S.contains("return Hush::Reflection::Variant()"));
}

// ---------------------------------------------------------------------------
// 11. FunctionNonVoidReturn
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FunctionNonVoidReturn) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  FunctionModel Func;
  Func.Name = "getValue";
  Func.ParentClassName = "Hush::Foo";
  Func.ReturnsVoid = false;
  Model.Functions.push_back(Func);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("auto result ="));
  EXPECT_TRUE(S.contains("return Hush::Reflection::Variant(result)"));
}

// ---------------------------------------------------------------------------
// 12. FunctionWithParams
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FunctionWithParams) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Foo";
  Model.UnqualifiedName = "Foo";

  FunctionModel Func;
  Func.Name = "act";
  Func.ParentClassName = "Hush::Foo";
  Func.ReturnsVoid = true;

  ParamModel Param;
  Param.TypeName = "int";
  Param.CanonicalTypeName = "int";
  Param.IsPointer = false;
  Func.Params.push_back(Param);
  Model.Functions.push_back(Func);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  // 1 param + 1 instance = 2
  EXPECT_TRUE(S.contains("args.size() != 2"));
  EXPECT_TRUE(S.contains("args[1].Get<"));
}

// ---------------------------------------------------------------------------
// 13. SerializationMultipleFields
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, SerializationMultipleFields) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Entity";
  Model.UnqualifiedName = "Entity";

  for (const char *Name : {"health", "stamina"}) {
    FieldModel Field;
    Field.Name = Name;
    Field.TypeName = "float";
    Field.ParentClassName = "Hush::Entity";
    Field.VisitorFieldName = std::string(Name) + "Visitor";
    Model.Fields.push_back(Field);
  }

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("Serialize<std::string_view>(\"__type\""));
  EXPECT_TRUE(S.contains("\"health\""));
  EXPECT_TRUE(S.contains("\"stamina\""));
}

// ---------------------------------------------------------------------------
// 14. DeserializationZeroFields
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, DeserializationZeroFields) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Empty";
  Model.UnqualifiedName = "Empty";

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("SetStartingVisitor(GetParentVisitor())"));
}

// ---------------------------------------------------------------------------
// 15. DeserializationOneField
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, DeserializationOneField) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Point";
  Model.UnqualifiedName = "Point";

  FieldModel Field;
  Field.Name = "x";
  Field.TypeName = "float";
  Field.ParentClassName = "Hush::Point";
  Field.VisitorFieldName = "xVisitor";
  Model.Fields.push_back(Field);

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("xVisitor"));
  EXPECT_TRUE(S.contains("VisitKey"));
  EXPECT_TRUE(S.contains("\"x\""));
}

// ---------------------------------------------------------------------------
// 16. DeserializationTwoFields
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, DeserializationTwoFields) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Vec2";
  Model.UnqualifiedName = "Vec2";

  for (const char *Name : {"a", "b"}) {
    FieldModel Field;
    Field.Name = Name;
    Field.TypeName = "float";
    Field.ParentClassName = "Hush::Vec2";
    Field.VisitorFieldName = std::string(Name) + "Visitor";
    Model.Fields.push_back(Field);
  }

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("aVisitor"));
  EXPECT_TRUE(S.contains("bVisitor"));
  // Enum entries should be uppercased
  EXPECT_TRUE(S.contains("A,"));
  EXPECT_TRUE(S.contains("B,"));
}

// ---------------------------------------------------------------------------
// 17. DeserializationThreeFields
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, DeserializationThreeFields) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Vec3";
  Model.UnqualifiedName = "Vec3";

  for (const char *Name : {"x", "y", "z"}) {
    FieldModel Field;
    Field.Name = Name;
    Field.TypeName = "float";
    Field.ParentClassName = "Hush::Vec3";
    Field.VisitorFieldName = std::string(Name) + "Visitor";
    Model.Fields.push_back(Field);
  }

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  EXPECT_TRUE(S.contains("xVisitor"));
  EXPECT_TRUE(S.contains("yVisitor"));
  EXPECT_TRUE(S.contains("zVisitor"));
  // Visitor chain: middle field links to previous
  EXPECT_TRUE(S.contains("yVisitor.SetParentVisitor(&xVisitor)"));
}

// ---------------------------------------------------------------------------
// 18. QualifiedVsUnqualifiedName
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, QualifiedVsUnqualifiedName) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Transform";
  Model.UnqualifiedName = "Transform";

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  // TypeName() should return the unqualified name
  EXPECT_TRUE(S.contains("\"Transform\""));
  // RegisterClass and Serialize use the qualified name
  EXPECT_TRUE(S.contains("RegisterClass<Hush::Transform>"));
  EXPECT_TRUE(S.contains("Hush::Transform"));
}

// ---------------------------------------------------------------------------
// 19. FullClassIntegration
// ---------------------------------------------------------------------------
TEST(ReflectionEmitterTest, FullClassIntegration) {
  ClassModel Model;
  Model.QualifiedName = "Hush::Actor";
  Model.UnqualifiedName = "Actor";

  // 2 fields
  for (const char *Name : {"hp", "mp"}) {
    FieldModel Field;
    Field.Name = Name;
    Field.TypeName = "float";
    Field.ParentClassName = "Hush::Actor";
    Field.VisitorFieldName = std::string(Name) + "Visitor";
    Model.Fields.push_back(Field);
  }

  // 1 constructor with 1 param
  {
    ConstructorModel Ctor;
    Ctor.ParentClassName = "Hush::Actor";
    ParamModel Param;
    Param.TypeName = "float";
    Param.CanonicalTypeName = "float";
    Param.IsPointer = false;
    Ctor.Params.push_back(Param);
    Model.Constructors.push_back(Ctor);
  }

  // 1 void function with 0 params
  {
    FunctionModel Func;
    Func.Name = "reset";
    Func.ParentClassName = "Hush::Actor";
    Func.ReturnsVoid = true;
    Model.Functions.push_back(Func);
  }

  std::string Output = emitToString(Model);
  llvm::StringRef S(Output);

  // Reflection section
  EXPECT_TRUE(S.contains("TypeName()"));
  EXPECT_TRUE(S.contains("TypeId()"));
  EXPECT_TRUE(S.contains("RegisterReflection"));
  EXPECT_TRUE(S.contains("RegisterClass<Hush::Actor>"));

  // Constructor section
  EXPECT_TRUE(S.contains("AddConstructor"));
  EXPECT_TRUE(S.contains("AddInPlaceConstructor"));
  EXPECT_TRUE(S.contains("std::construct_at"));

  // Function section
  EXPECT_TRUE(S.contains("AddFunction"));
  EXPECT_TRUE(S.contains("instance->reset("));
  EXPECT_TRUE(S.contains("return Hush::Reflection::Variant()"));

  // Field section
  EXPECT_TRUE(S.contains(".AddProperty("));
  EXPECT_TRUE(S.contains("\"hp\""));
  EXPECT_TRUE(S.contains("\"mp\""));
  EXPECT_TRUE(S.contains(".Register()"));

  // Serialization section
  EXPECT_TRUE(S.contains("Serialize<std::string_view>(\"__type\""));

  // Deserialization section
  EXPECT_TRUE(S.contains("Deserialize"));
  EXPECT_TRUE(S.contains("hpVisitor"));
  EXPECT_TRUE(S.contains("mpVisitor"));

  EXPECT_TRUE(S.contains("private:"));
}

} // namespace
