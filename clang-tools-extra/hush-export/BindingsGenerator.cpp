#include "BindingsGenerator.h"

#include "ParserCommon.h"

#include "llvm/DebugInfo/PDB/PDBTypes.h"

const std::set<std::string> SpecialTypes = {"glm::vec", "glm::mat", "glm::quat",
                                            "glm::transform", "glm::color"};

const std::array<std::string, 1> SpecialNamespaces = {"glm"};

struct FieldOptions {
  bool Ignore;
  std::string Name;
};

static bool isPOD(const clang::RecordDecl *D) {
  const auto &Context = D->getASTContext();
  clang::QualType RecordType = Context.getRecordType(D);

  // Check if the type is a POD type
  return RecordType.isPODType(Context);
}

FieldOptions getMemberFieldOptions(const clang::FieldDecl *Field);

void hush::HushBindingMatcher::run(
    const clang::ast_matchers::MatchFinder::MatchResult &Result) {
  if (const clang::RecordDecl *D =
          Result.Nodes.getNodeAs<clang::RecordDecl>("hushExportable")) {
    clang::HushExportAttr *HushExportAttr = D->getAttr<clang::HushExportAttr>();
    processClassDecl(HushExportAttr, D);
  } else if (const clang::TypedefNameDecl *TD =
                 Result.Nodes.getNodeAs<clang::TypedefNameDecl>("glmVecDecl")) {
    const auto Name = TD->getNameAsString();

  } else if (const clang::FunctionDecl *FD =
                 Result.Nodes.getNodeAs<clang::FunctionDecl>(
                     "hushExportable")) {
    clang::HushExportAttr *HushExportAttr =
        FD->getAttr<clang::HushExportAttr>();
    processFunctionDecl(HushExportAttr, FD);
  } else if (const clang::EnumDecl *ED =
                 Result.Nodes.getNodeAs<clang::EnumDecl>("hushExportable")) {
    clang::HushExportAttr *HushExportAttr =
        ED->getAttr<clang::HushExportAttr>();
    processEnumDecl(HushExportAttr, ED);
  }
}

void hush::HushBindingMatcher::addSpecialTypes() {
  // Special types:
  // - glm::vec2, glm::vec3, glm::vec4
  // - glm::dvec2, glm::dvec3, glm::dvec4
  // - glm::u8vec2, glm::u8vec3, glm::u8vec4
  // - glm::i8vec2, glm::i8vec3, glm::i8vec4
  // - glm::u16vec2, glm::u16vec3, glm::u16vec4
  // - glm::i16vec2, glm::i16vec3, glm::i16vec4
  // - glm::u32vec2, glm::u32vec3, glm::u32vec4
  // - glm::i32vec2, glm::i32vec3, glm::i32vec4
  // - glm::u64vec2, glm::u64vec3, glm::u64vec4
  // - glm::i64vec2, glm::i64vec3, glm::i64vec4
  // - glm::mat2, glm::mat3, glm::mat4
  // - glm::dmat2, glm::dmat3, glm::dmat4
  // - glm::quat

  // Let's start by adding the special types to the list
  struct VecDef {
    std::string_view Name;
    std::string_view ExportedName;
    int Size;
  };
  constexpr std::array BaseVecDefs{VecDef{"vec2", "Vector2", 2},
                                   VecDef{"vec3", "Vector3", 3},
                                   VecDef{"vec4", "Vector4", 4}};

  constexpr std::array Sizes{std::string_view{"8"}, std::string_view{"16"},
                             std::string_view{"32"}, std::string_view{"64"}};

  constexpr std::array VecFields{std::string_view{"x"}, std::string_view{"y"},
                                 std::string_view{"z"}, std::string_view{"w"}};

  for (const auto &BaseDef : BaseVecDefs) {
    ExportedTypeInfo BaseTypeInfo;
    std::shared_ptr<ExportedClass> NewClass = std::make_shared<ExportedClass>();
    NewClass->Name = "glm::" + std::string(BaseDef.Name);
    NewClass->ExportedName = std::string(BaseDef.ExportedName);

    for (size_t I = 0; I < BaseDef.Size; ++I) {
      auto Field = ClassMemberVariable{};
      Field.Name = std::string(VecFields[I]);
      Field.Type = "float";
      Field.IsHidden = false;
      Field.Alignment = 8;
      Field.Size = 8;

      NewClass->Members.push_back(Field);
    }

    BaseTypeInfo.Name = NewClass->Name;
    BaseTypeInfo.ExportedName = NewClass->ExportedName;
    BaseTypeInfo.TypeData = NewClass;

    this->ParsedClasses.insert({BaseTypeInfo.Name, BaseTypeInfo});
    this->ParsedClassesVector.push_back(NewClass);
  }

  // Add the double types
  for (const auto &BaseDef : BaseVecDefs) {
    ExportedTypeInfo BaseTypeInfo;
    std::shared_ptr<ExportedClass> NewClass = std::make_shared<ExportedClass>();
    NewClass->Name = "glm::d" + std::string(BaseDef.Name);
    NewClass->ExportedName = "D" + std::string(BaseDef.ExportedName);

    for (size_t I = 0; I < BaseDef.Size; ++I) {
      auto Field = ClassMemberVariable{};
      Field.Name = std::string(VecFields[I]);
      Field.Type = "double";
      Field.IsHidden = false;
      Field.Alignment = 8;
      Field.Size = 8;

      NewClass->Members.push_back(Field);
    }

    BaseTypeInfo.Name = NewClass->Name;
    BaseTypeInfo.ExportedName = NewClass->ExportedName;
    BaseTypeInfo.TypeData = NewClass;

    this->ParsedClasses.insert({BaseTypeInfo.Name, BaseTypeInfo});
    this->ParsedClassesVector.push_back(NewClass);
  }

  // Add the signed and unsigned types
  for (const auto &BaseDef : BaseVecDefs) {
    for (const auto &Size : Sizes) {
      ExportedTypeInfo UnsignedBaseTypeInfo;
      ExportedTypeInfo SignedBaseTypeInfo;
      std::shared_ptr<ExportedClass> NewUnsigned =
          std::make_shared<ExportedClass>();
      std::shared_ptr<ExportedClass> NewSigned =
          std::make_shared<ExportedClass>();

      NewUnsigned->Name = "glm::u" + std::string(Size) + BaseDef.Name.data();
      NewUnsigned->ExportedName =
          "U" + std::string(Size) + BaseDef.ExportedName.data();

      NewSigned->Name = "glm::i" + std::string(Size) + BaseDef.Name.data();
      NewSigned->ExportedName =
          "I" + std::string(Size) + BaseDef.ExportedName.data();

      for (size_t I = 0; I < BaseDef.Size; ++I) {
        auto UnsignedField = ClassMemberVariable{};
        UnsignedField.Name = std::string(VecFields[I]);
        UnsignedField.Type = "uint" + std::string(Size) + "_t";
        UnsignedField.IsHidden = false;
        UnsignedField.Alignment = 8;
        UnsignedField.Size = 8;

        auto SignedField = ClassMemberVariable{};
        SignedField.Name = std::string(VecFields[I]);
        SignedField.Type = "int" + std::string(Size) + "_t";
        SignedField.IsHidden = false;
        SignedField.Alignment = 8;
        SignedField.Size = 8;

        NewUnsigned->Members.push_back(UnsignedField);
        NewSigned->Members.push_back(SignedField);
      }

      UnsignedBaseTypeInfo.Name = NewUnsigned->Name;
      UnsignedBaseTypeInfo.ExportedName = NewUnsigned->ExportedName;
      UnsignedBaseTypeInfo.TypeData = NewUnsigned;
      SignedBaseTypeInfo.Name = NewSigned->Name;
      SignedBaseTypeInfo.ExportedName = NewSigned->ExportedName;
      SignedBaseTypeInfo.TypeData = NewSigned;

      this->ParsedClasses.insert(
          {UnsignedBaseTypeInfo.Name, UnsignedBaseTypeInfo});
      this->ParsedClasses.insert({SignedBaseTypeInfo.Name, SignedBaseTypeInfo});
      this->ParsedClassesVector.push_back(NewUnsigned);
      this->ParsedClassesVector.push_back(NewSigned);
    }
  }

  // Add the float matrix types
  for (int i = 2; i <= 4; ++i) {
    ExportedTypeInfo BaseTypeInfo;
    std::shared_ptr<ExportedClass> NewClass = std::make_shared<ExportedClass>();
    NewClass->Name = "glm::mat" + std::to_string(i);
    NewClass->ExportedName = "Matrix" + std::to_string(i);
    NewClass->IsHandle = false;
    NewClass->IsNonPOD = false;

    NewClass->Members.push_back(ClassMemberVariable{
        "m[" + std::to_string(i) + "]", "float", 0, 0, false});

    this->ParsedClasses.insert({NewClass->Name, BaseTypeInfo});
    this->ParsedClassesVector.push_back(NewClass);
  }

  for (int i = 2; i <= 4; ++i) {
    ExportedTypeInfo BaseTypeInfo;
    std::shared_ptr<ExportedClass> NewClass = std::make_shared<ExportedClass>();
    NewClass->Name = "glm::dmat" + std::to_string(i);
    NewClass->ExportedName = "DMatrix" + std::to_string(i);
    NewClass->IsHandle = false;
    NewClass->IsNonPOD = false;

    NewClass->Members.push_back(ClassMemberVariable{
        "m[" + std::to_string(i) + "]", "double", 0, 0, false});

    this->ParsedClasses.insert({NewClass->Name, BaseTypeInfo});
    this->ParsedClassesVector.push_back(NewClass);
  }

  // Add the quaternion types
  ExportedTypeInfo BaseTypeInfo;
  std::shared_ptr<ExportedClass> NewClass = std::make_shared<ExportedClass>();
  NewClass->Name = "glm::quat";
  NewClass->ExportedName = "Quat";
  NewClass->IsHandle = false;
  NewClass->IsNonPOD = false;

  for (const auto &Field : VecFields) {
    auto ClassField = ClassMemberVariable{};
    ClassField.Name = std::string(Field);
    ClassField.Type = "float";
    ClassField.IsHidden = false;
    ClassField.Alignment = 4;
    ClassField.Size = 4;

    NewClass->Members.push_back(ClassField);
  }

  this->ParsedClasses.insert({NewClass->Name, BaseTypeInfo});
  this->ParsedClassesVector.push_back(NewClass);
}

void hush::HushBindingMatcher::processClassDecl(
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D) {

  if (!D->isCompleteDefinition()) {
    return;
  }

  if (D->isDependentType()) {
    return;
  }

  if (D->isInvalidDecl()) {
    return;
  }

  if (HushExportAttr == nullptr) {
    return;
  }

  processHushExportClassDecl(HushExportAttr, D);
}

void hush::HushBindingMatcher::processFunctionDecl(
    const clang::HushExportAttr *HushExportAttr, const clang::FunctionDecl *D) {

  if (!HushExportAttr) {
    return;
  }

  // Check if we already have this function
  for (const auto &Func : this->Functions) {
    if (Func.Name == D->getQualifiedNameAsString()) {
      return;
    }
  }

  std::string ExportName;
  std::string FullFunctionName = D->getQualifiedNameAsString();

  auto *AttributeArgsBegin = HushExportAttr->exportConfig_begin();
  auto *AttributeArgsEnd = HushExportAttr->exportConfig_end();

  for (clang::Expr **ArgExpr = AttributeArgsBegin; ArgExpr != AttributeArgsEnd;
       ++ArgExpr) {
    clang::DeclRefExpr *ArgRef =
        llvm::dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());

    if (ArgRef != nullptr) {
      if (isHushExportIgnore(ArgRef, D->getASTContext())) {
        // Ignore this function.
        return;
      }
    }

    clang::CallExpr *ArgCall =
        llvm::dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
    if (ArgCall != nullptr) {
      if (auto Name = getHushExportName(ArgCall, D->getASTContext());
          Name.has_value()) {
        ExportName = *Name;
      }
    }
  }

  if (ExportName.empty()) {
    ExportName = D->getQualifiedNameAsString();
    // Replace :: with _
    std::replace(ExportName.begin(), ExportName.end(), ':', '_');
  }

  auto FuncInfo = FunctionInfo{};
  FuncInfo.Name = FullFunctionName;
  FuncInfo.ExportedName = ExportName;

  // Check if a class owns this function
  if (const auto *Parent = D->getParent();
      Parent && Parent->isRecord() && !D->isStatic()) {
    auto *Record = llvm::dyn_cast<clang::RecordDecl>(Parent);
    auto FullyQualifiedName = Record->getQualifiedNameAsString();

    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
      return;
    }

    FuncInfo.ContainingClass = std::get<std::shared_ptr<ExportedClass>>(
        AlreadyExported->second.TypeData);
  }

  auto ReturnType = D->getReturnType();

  if (!processFuncReturnType(D, FuncInfo, ReturnType))
    return;

  for (const clang::ParmVarDecl *Param : D->parameters()) {
    // If the parameter is a record, we need to check if it is already parsed
    auto FuncParam = FunctionParam{};
    FuncParam.Name = Param->getNameAsString();
    FuncParam.IsPointer = Param->getType()->isPointerType();
    FuncParam.IsReference = Param->getType()->isReferenceType();

    // First, check if the parameter has a HushExport attribute
    auto *ParamHushExportAttr = Param->getAttr<clang::HushExportAttr>();

    if (ParamHushExportAttr) {
      AttributeArgsBegin = ParamHushExportAttr->exportConfig_begin();
      AttributeArgsEnd = ParamHushExportAttr->exportConfig_end();

      for (clang::Expr **ArgExpr = AttributeArgsBegin;
           ArgExpr != AttributeArgsEnd; ++ArgExpr) {
        clang::DeclRefExpr *ArgRef =
            llvm::dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());
      }
    }

    if (Param->getType()->isRecordType()) {
      if (!processRecordTypeParam(D, Param, FuncParam))
        return;
    } else if (Param->getType()->isPointerType()) {
      if (!processPointerParam(D, Param, FuncParam)) {
        return;
      }
    } else if (Param->getType()->isReferenceType()) {
      if (!processReferenceParam(D, Param, FuncParam)) {
        return;
      }
    } else if (Param->getType()->isBuiltinType()) {
      FuncParam.Type = Param->getType().getCanonicalType().getAsString();
    } else if (Param->getType()->isEnumeralType()) {

      auto FullyQualifiedName =
          Param->getType().getCanonicalType().getAsString();

      FuncParam.Type = FullyQualifiedName;
      FuncParam.RealType = FullyQualifiedName;
      // FuncParam.EnumType = FullyQualifiedName;

      std::replace(FuncParam.EnumType.begin(), FuncParam.EnumType.end(), ':', '_');
    }

    // Add the parameter
    FuncInfo.Parameters.push_back(FuncParam);
  }

  this->Functions.push_back(FuncInfo);
}

void hush::HushBindingMatcher::processEnumDecl(
    const clang::HushExportAttr *HushExportAttr, const clang::EnumDecl *D) {
  // Processing an enum is easy, first, we need to check if the enum has its
  // type specified, for instance, enum class MyEnum : uint32_t { ... };, if the
  // type is specified, we should export it as a typedef, otherwise, we should
  // export it as an enum.

  if (HushExportAttr == nullptr) {
    return;
  }

  std::string EnumName = D->getQualifiedNameAsString();
  std::string ExportedName = EnumName;

  // Replace each :: with _
  std::replace(ExportedName.begin(), ExportedName.end(), ':', '_');

  // Process the attribute arguments
  auto *AttributeArgsBegin = HushExportAttr->exportConfig_begin();
  auto *AttributeArgsEnd = HushExportAttr->exportConfig_end();
  for (clang::Expr **ArgExpr = AttributeArgsBegin; ArgExpr != AttributeArgsEnd;
       ++ArgExpr) {
    clang::DeclRefExpr *ArgRef =
        llvm::dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());

    if (ArgRef != nullptr) {
      if (isHushExportIgnore(ArgRef, D->getASTContext())) {
        // Ignore this class.
        return;
      }
    }

    clang::CallExpr *ArgCall =
        llvm::dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
    if (ArgCall != nullptr) {
      if (auto Name = getHushExportName(ArgCall, D->getASTContext());
          Name.has_value()) {
        ExportedName = *Name;
      }
    }
  }

  auto EnumData = std::make_shared<EnumDeclaration>();
  auto ExportedTypeData = ExportedTypeInfo{};
  EnumData->Name = EnumName;
  EnumData->ExportedName = ExportedName;
  ExportedTypeData.Name = EnumName;
  ExportedTypeData.ExportedName = ExportedName;
  ExportedTypeData.TypeData = EnumData;

  // Check if the enum has a type
  if (D->getIntegerTypeSourceInfo() == nullptr) {
    // We don't have a specified type, so we need to export it as an enum
    EnumData->IsPlainEnum = true;
  } else {
    // We have a specified type, so we need to export it as a typedef
    EnumData->IsPlainEnum = false;
    EnumData->InnerType = D->getIntegerType().getAsString();
  }

  // Process the enum values
  for (const auto *EnumConstant : D->enumerators()) {
    const std::string Name = EnumConstant->getName().str();
    const int64_t Value = EnumConstant->getInitVal().getExtValue();
    EnumData->EnumValues.push_back({Name, Value});
  }

  // Add the enum to the parsed enums
  ParsedEnums.push_back(EnumData);
  ParsedClasses[EnumName] = ExportedTypeData;
}

void hush::HushBindingMatcher::processSpecialTypeDecl(const clang::QualType D) {
  // Check if the type is in the glm namespace
  std::string FullTypeName = D.getAsString();
  std::string ExposedTypeName = FullTypeName;

  if (this->ParsedClasses.find(FullTypeName) != this->ParsedClasses.end()) {
    return;
  }

  if (ExposedTypeName.find("glm") != std::string::npos) {
    // Okay, we need to export this type as the following:
    // glm::vec3 -> typedef struct Vector3 { float x, y, z; } Vector3;
    // glm::i32vec3 -> typedef struct i32Vector3 { int32_t x, y, z; }
    // i32Vector3; glm::ivec3 -> typedef struct i32Vector3 { int32_t x, y, z; }
    // i32Vector3; glm::mat4 -> typedef struct Matrix4 { float m[16]; } Matrix4;
    // glm::mat4x4 -> typedef struct Matrix4 { float m[16]; } Matrix4;

    auto ExportedClassInfo = std::make_shared<ExportedClass>();
    auto ExportedTypeData = ExportedTypeInfo{};

    ExposedTypeName.replace(ExposedTypeName.find("glm::"), 5, "");

    std::size_t TypeTemplateIndex;

    if (auto VecPos = ExposedTypeName.find("vec");
        VecPos != std::string::npos) {
      TypeTemplateIndex = 1;
      ExposedTypeName.replace(ExposedTypeName.find("vec"), 3, "Vector");
    } else if (const auto MatPos = ExposedTypeName.find("mat");
               MatPos != std::string::npos) {
      TypeTemplateIndex = 2;
      ExposedTypeName.replace(ExposedTypeName.find("mat"), 3, "Matrix");
    } else if (auto QuatPos = ExposedTypeName.find("quat");
               QuatPos != std::string::npos) {
      TypeTemplateIndex = 0;
      ExposedTypeName.replace(ExposedTypeName.find("quat"), 4, "Quaternion");
    } else {
      llvm::errs() << "Could not find the type of the glm type\n";
      // We don't know how to export this type
      return;
    }
    ExportedTypeData.Name = FullTypeName;
    ExportedTypeData.ExportedName = ExposedTypeName;
    ExportedClassInfo->IsHandle = false;
    ExportedClassInfo->Name = FullTypeName;
    ExportedClassInfo->ExportedName = ExposedTypeName;

    // Get the record declaration
    const clang::RecordDecl *RD = D->getAsRecordDecl();
    if (RD == nullptr) {
      clang::DiagnosticsEngine &DiagEngine =
          RD->getASTContext().getDiagnostics();
      unsigned DiagID = DiagEngine.getCustomDiagID(
          clang::DiagnosticsEngine::Error,
          "Could not get the record declaration for %0");

      DiagEngine.Report(RD->getLocation(), DiagID) << FullTypeName;
      return;
    }

    // Get the template type
    const clang::TemplateSpecializationType *TemplateType =
        D->getAs<clang::TemplateSpecializationType>();

    std::string TemplateTypeStr;
    ArrayRef<clang::TemplateArgument> TemplateArgs =
        TemplateType->template_arguments();
    std::string FieldTypeStr =
        TemplateArgs[TypeTemplateIndex].getAsType().getAsString();
    // for (clang::TemplateArgument TemplateArg :
    //      TemplateType->template_arguments()) {
    //   TemplateTypeStr = TemplateArg.getAsType().getAsString();
    // }

    // If the field is std::uint* or std::int*, we need to remove the std::

    for (const auto *Field : RD->fields()) {
      clang::QualType QualifiedType = Field->getType();

      // std::string FieldTypeStr = qualifiedType.getAsString();

      auto MemberVariable = ClassMemberVariable{};
      MemberVariable.Name = Field->getName().str();
      MemberVariable.Type = FieldTypeStr;
      MemberVariable.Alignment =
          RD->getASTContext().getTypeAlign(QualifiedType);
      MemberVariable.Size = RD->getASTContext().getTypeSize(QualifiedType);

      ExportedClassInfo->Members.push_back(MemberVariable);
    }

    ExportedTypeData.TypeData = ExportedClassInfo;

    this->ParsedClasses.insert({FullTypeName, ExportedTypeData});
    this->ParsedClassesVector.push_back(ExportedClassInfo);
  }
}

void hush::HushBindingMatcher::processHushExportClassDecl(
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D) {
  // First, get the file path
  clang::ASTContext &Context = D->getASTContext();

  // Okay, so we need to check the arguments of the attribute.
  // Get fully qualified name
  std::string FullyQualifiedName = D->getQualifiedNameAsString();
  std::string ExportedName = FullyQualifiedName;

  // Replace :: with _
  std::replace(ExportedName.begin(), ExportedName.end(), ':', '_');

  // Check if the class is already parsed
  if (this->ParsedClasses.find(FullyQualifiedName) !=
      this->ParsedClasses.end()) {
    return;
  }

  bool IsHandle = false;
  if (HushExportAttr) {
    auto *AttributeArgsBegin = HushExportAttr->exportConfig_begin();
    auto *AttributeArgsEnd = HushExportAttr->exportConfig_end();

    // Iterate over the attribute arguments
    for (clang::Expr **ArgExpr = AttributeArgsBegin;
         ArgExpr != AttributeArgsEnd; ++ArgExpr) {
      // Check if the argument is a reference to a variable called asHandle in
      // nms Hush::Export
      clang::DeclRefExpr *ArgRef =
          dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());

      if (ArgRef != nullptr) {
        if (isHushExportIgnore(ArgRef, Context)) {
          // Ignore this class.
          return;
        }
        if (isHushExportHandle(ArgRef, Context)) {
          IsHandle = true;
        }
      }
    }
  }

  // Then parse the class, if it is not transparent, parsing is simple, just
  // export it as a pointer (ang alignas as the size of the record) If it is
  // transparent, we need to parse the fields and export them as well
  if (IsHandle) {
    // It is simple, export as a pointer
    auto NewClass = std::make_shared<ExportedClass>();
    auto ExportedTypeData = ExportedTypeInfo{};
    ExportedTypeData.Name = FullyQualifiedName;
    ExportedTypeData.ExportedName = ExportedName;
    NewClass->IsHandle = true;
    NewClass->Name = FullyQualifiedName;
    NewClass->ExportedName = ExportedName;

    ExportedTypeData.TypeData = NewClass;

    this->ParsedClasses[FullyQualifiedName] = ExportedTypeData;
    this->ParsedClassesVector.push_back(NewClass);

    return;
  }

  auto NewClass = std::make_shared<ExportedClass>();
  auto ExportedTypeData = ExportedTypeInfo{};

  ExportedTypeData.Name = FullyQualifiedName;
  ExportedTypeData.ExportedName = ExportedName;
  ExportedTypeData.TypeData = NewClass;
  NewClass->Name = FullyQualifiedName;
  NewClass->ExportedName = ExportedName;

  // Useful when generating a destructor.
  NewClass->IsNonPOD = !isPOD(D);

  // Process each one of the fields
  for (const auto *Field : D->fields()) {
    // Get the field type
    const clang::QualType FieldType = Field->getType();

    // Check if the field is private or protected, if it is, issue a warning
    if (Field->getAccess() != clang::AccessSpecifier::AS_public) {

      // Create a random name for the field

      // Private members should be exported as a char buffer with specific
      // alignment
      auto NewField = ClassMemberVariable{};
      NewField.Name = "m_member" + std::to_string(NewClass->Members.size());
      NewField.Alignment = Context.getTypeAlign(FieldType) / 8;
      NewField.Size = Context.getTypeSize(FieldType) / 8;
      NewField.IsHidden = true;

      NewClass->Members.push_back(NewField);

      continue;
    }

    // Check if the field is a special type, they could be a template, so
    // remove that part
    std::string FieldTypeStr = FieldType.getAsString();
    size_t TemplatePos = FieldTypeStr.find('<');

    if (TemplatePos != std::string::npos) {
      FieldTypeStr = FieldTypeStr.substr(0, TemplatePos);
    }

    // Check if the name is part of the special namespaces
    bool IsSpecialType = false;
    for (const auto &Namespace : SpecialNamespaces) {
      if (FieldTypeStr.find(Namespace) != std::string::npos) {
        IsSpecialType = true;
        break;
      }
    }

    FieldOptions FieldOptions = getMemberFieldOptions(Field);

    if (FieldOptions.Ignore) {
      // Issue an error
      unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error,
          "Field %0 is ignored, this is not supported when generating the "
          "bindings");

      clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
      DiagEngine.Report(Field->getLocation(), DiagID) << Field->getName();

      return;
    }

    auto NewField = ClassMemberVariable{};
    NewField.Name = FieldOptions.Name;
    NewField.Alignment = Context.getTypeAlign(FieldType);
    NewField.Size = Context.getTypeSize(FieldType);

    if (Field->isFunctionPointerType()) {
      NewField.IsFunctionPointer = true;
    }

    if (IsSpecialType) {
      // We already processed this type, find it
      const auto &AlreadyExported = this->ParsedClasses.find(FieldTypeStr);

      if (AlreadyExported == this->ParsedClasses.end()) {
        unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Field %0 is of type %1, which is not a Hush type");

        clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
        DiagEngine.Report(Field->getLocation(), DiagID)
            << Field->getName() << FieldTypeStr;
        return;
      }

      NewField.Type = AlreadyExported->second.ExportedName;
    } else {

      // Okay, check if it is a record decl
      // Get the field fully qualified name
      std::string FieldFullyQualifiedName =
          FieldType.getCanonicalType().getAsString(Context.getLangOpts());

      // Replace the field fully qualified by changing the :: to _
      std::replace(FieldFullyQualifiedName.begin(),
                   FieldFullyQualifiedName.end(), ':', '_');

      if (!FieldType->isBuiltinType()) {
        // Get the field type without pointers, const, volatile, etc.
        const clang::QualType FieldTypeNoCV = FieldType.getUnqualifiedType();
        std::string FieldTypeName = FieldTypeNoCV.getCanonicalType().getAsString();
        if (auto FoundHush = FieldTypeName.find("Hush"); FoundHush == 0) {
          unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "Field %0 is of type %1, which is not a Hush type");

          clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
          DiagEngine.Report(Field->getLocation(), DiagID)
              << Field->getName() << FieldFullyQualifiedName;

          return;
        }
      }

      NewField.Type = FieldFullyQualifiedName;
    }

    NewClass->Members.push_back(NewField);
  }

  this->ParsedClasses.insert(
      std::make_pair(FullyQualifiedName, ExportedTypeData));
  this->ParsedClassesVector.push_back(NewClass);
}

bool hush::HushBindingMatcher::processPointerRet(
    const clang::FunctionDecl *FunctionDeclInfo, ReturnTypeInfo &ReturnType) {

  // Get the return type
  const clang::QualType ReturnTypeDecl = FunctionDeclInfo->getReturnType();
  clang::QualType PointeeType = ReturnTypeDecl->getPointeeType();

  while (PointeeType->isPointerType()) {
    PointeeType = PointeeType->getPointeeType();
  }

  // Get the inner type name withou const, volatile, etc.
  const clang::QualType InnerTypeNoCV = PointeeType.getUnqualifiedType();
  std::string InnerTypeName = InnerTypeNoCV.getAsString(
      FunctionDeclInfo->getASTContext().getPrintingPolicy());

  if (PointeeType->isRecordType()) {
    // Okay, it is a record, check if it is already parsed
    std::string FullyQualifiedName =
        PointeeType->getAsRecordDecl()->getQualifiedNameAsString();
    std::string ReturnTypeName = ReturnTypeDecl.getAsString();

    // Get also the record declaration
    const clang::RecordDecl *RecordDecl = PointeeType->getAsRecordDecl();

    if (RecordDecl == nullptr) {
      unsigned int DiagID =
          FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "Return type %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          FunctionDeclInfo->getASTContext().getDiagnostics();
      DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
          << ReturnTypeName;
      return false;
    }

    // Check if it is part of the special namespaces
    for (const auto &Namespace : SpecialNamespaces) {
      if (FullyQualifiedName.find(Namespace) != std::string::npos) {
        processSpecialTypeDecl(PointeeType);
        break;
      }
    }

    // Okay, a normal type, check if it has a HushExport attribute
    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      auto *HushExportAttr = RecordDecl->getAttr<clang::HushExportAttr>();

      if (HushExportAttr == nullptr) {
        unsigned DiagID =
            FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "Return type %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            FunctionDeclInfo->getASTContext().getDiagnostics();
        DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
            << FullyQualifiedName;
        return false;
      }

      processHushExportClassDecl(HushExportAttr, RecordDecl);
    }

    // Find it again
    AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID =
          FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "Return type %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          FunctionDeclInfo->getASTContext().getDiagnostics();

      DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
          << FullyQualifiedName;

      return false;
    }

    // It is already exported.
    std::string FullReturnTypeName = ReturnTypeDecl.getAsString(
        FunctionDeclInfo->getASTContext().getLangOpts());
    std::string FullInnerTypeName = PointeeType.getAsString(
        FunctionDeclInfo->getASTContext().getLangOpts());

    std::string ReturnTypeStr = FullReturnTypeName;
    ReturnTypeStr.replace(ReturnTypeStr.find(InnerTypeName),
                          InnerTypeName.size(),
                          AlreadyExported->second.ExportedName);

    ReturnType.Type = ReturnTypeStr;
  } else if (PointeeType->isBuiltinType()) {
    // Just export the type by substituting the type
    std::string ReturnTypeStr = InnerTypeName;
    std::string PointerTypeStr = ReturnTypeDecl->getPointeeType().getAsString(
        FunctionDeclInfo->getASTContext().getLangOpts());

    PointerTypeStr.replace(PointerTypeStr.find(InnerTypeName),
                           InnerTypeName.size(), ReturnTypeStr);

    ReturnType.Type = PointerTypeStr;
  } else {
    unsigned int DiagID =
        FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Return type %0 is not exported\n");

    clang::DiagnosticsEngine &DiagEngine =
        FunctionDeclInfo->getASTContext().getDiagnostics();
    DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
        << PointeeType.getAsString();
    return false;
  }

  return true;
}

bool hush::HushBindingMatcher::processFuncReturnType(
    const clang::FunctionDecl *D, FunctionInfo &FuncInfo,
    clang::QualType ReturnType) {
  if (ReturnType->isBuiltinType()) {
    FuncInfo.ReturnType.Type = ReturnType.getCanonicalType().getAsString();
  } else {
    auto FullyQualifiedName = ReturnType.getCanonicalType().getAsString(
        D->getASTContext().getPrintingPolicy());

    // First, check if it as a special type
    if (FullyQualifiedName.find("std::span") != std::string::npos ||
        FullyQualifiedName.find("std::string_view") != std::string::npos ||
        FullyQualifiedName.find("std::vector") != std::string::npos ||
        FullyQualifiedName.find("std::string") != std::string::npos) {
      FuncInfo.ReturnType.Type = FullyQualifiedName;
      // Get the inner type
      auto InnerType = ReturnType->getAs<clang::TemplateSpecializationType>()
                           ->template_arguments()
                           .front()
                           .getAsType();

      std::string InnerTypeWithQualifiers =
          InnerType.getCanonicalType().getAsString(
              D->getASTContext().getPrintingPolicy());

      while (InnerType->isPointerType()) {
        InnerType = InnerType->getPointeeType();
      }

      auto InnerTypeNoCV = InnerType.getUnqualifiedType();

      auto InnerTypeName = InnerType.getCanonicalType().getAsString(
          D->getASTContext().getPrintingPolicy());

      auto InnerTypeNoCVName =
          InnerTypeNoCV.getAsString(D->getASTContext().getPrintingPolicy());

      // Check if the inner type is of a special type
      bool IsSpecialType = false;
      for (const auto &Namespace : SpecialNamespaces) {
        if (InnerTypeNoCVName.find(Namespace) != std::string::npos) {
          IsSpecialType = true;
          break;
        }
      }
      if (IsSpecialType) {
        auto AlreadyExported = this->ParsedClasses.find(InnerTypeNoCVName);
        if (AlreadyExported == this->ParsedClasses.end()) {
          // We don't have a HushExport attribute, so we can't export it
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "Return type %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
          return false;
        }

        FuncInfo.ReturnType.InnerType =
            InnerType.getAsString(D->getASTContext().getPrintingPolicy());
        // Next, we need to replace the find the InnerTypeNoCV and replace it
        // with the exported name

        FuncInfo.ReturnType.InnerType.replace(
            FuncInfo.ReturnType.InnerType.find(InnerTypeNoCVName),
            InnerTypeNoCVName.size(), AlreadyExported->second.ExportedName);

        // FuncInfo.ReturnType.InnerType = AlreadyExported->second.ExportedName;

      } else {
        FuncInfo.ReturnType.InnerType = InnerTypeWithQualifiers;
        std::replace(FuncInfo.ReturnType.InnerType.begin(),
                     FuncInfo.ReturnType.InnerType.end(), ':', '_');
      }
    } else {
      bool IsSpecialType = false;
      clang::QualType ReturnTypeNoCV = ReturnType;

      while (ReturnTypeNoCV->isPointerType()) {
        ReturnTypeNoCV = ReturnTypeNoCV->getPointeeType();
      }

      ReturnType = ReturnTypeNoCV.getUnqualifiedType();
      std::string RetTypeFullInnerName =
          ReturnType.getCanonicalType().getAsString(
              D->getASTContext().getPrintingPolicy());

      for (const auto &Namespace : SpecialNamespaces) {
        if (FullyQualifiedName.find(Namespace) != std::string::npos) {
          IsSpecialType = true;
          break;
        }
      }

      if (IsSpecialType) {
        // Okay, we have a class, check if it is already parsed
        auto AlreadyExported = this->ParsedClasses.find(RetTypeFullInnerName);
        if (AlreadyExported == this->ParsedClasses.end()) {
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "Return type %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
          return false;
        }

        FuncInfo.ReturnType.Type = FullyQualifiedName;

        FuncInfo.ReturnType.Type.replace(
            FuncInfo.ReturnType.Type.find(RetTypeFullInnerName),
            RetTypeFullInnerName.size(), AlreadyExported->second.ExportedName);

      } else {
        FuncInfo.ReturnType.Type = FullyQualifiedName;

        std::replace(FuncInfo.ReturnType.Type.begin(),
                     FuncInfo.ReturnType.Type.end(), ':', '_');
      }
    }
  }

  return true;
}

bool hush::HushBindingMatcher::processRecordTypeParam(
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {
  auto FullyQualifiedName = Param->getType().getAsString();

  // First, check if it as a special type
  if (FullyQualifiedName.find("std::span") != std::string::npos ||
      FullyQualifiedName.find("std::string_view")) {

    // Get the inner type
    clang::QualType InnerType = Param->getType()
                                    ->getAs<clang::TemplateSpecializationType>()
                                    ->template_arguments()
                                    .front()
                                    .getAsType();

    // If the inner type is a record, we need to check if it is already parsed
    if (InnerType->isRecordType()) {
      std::string InnerFullyQualifiedName = InnerType.getAsString();

      // Okay, we have a class, check if it is already parsed
      auto AlreadyExported = this->ParsedClasses.find(InnerFullyQualifiedName);
      if (AlreadyExported == this->ParsedClasses.end()) {
        // Get the record declaration
        const clang::RecordDecl *RD = InnerType->getAsRecordDecl();

        const clang::HushExportAttr *SpecialTypeAttr =
            RD->getAttr<clang::HushExportAttr>();

        if (SpecialTypeAttr == nullptr) {
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(Param->getLocation(), DiagID)
              << InnerFullyQualifiedName;
          return false;
        }

        processHushExportClassDecl(SpecialTypeAttr, RD);
        // Find it again
        AlreadyExported = this->ParsedClasses.find(InnerFullyQualifiedName);
        if (AlreadyExported == this->ParsedClasses.end()) {
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(Param->getLocation(), DiagID)
              << InnerFullyQualifiedName;
          return false;
        }
      }

      FuncParam.InnerType.Type = AlreadyExported->second.ExportedName;
    } else {
      FuncParam.InnerType.Type = InnerType.getAsString();
    }

    FuncParam.Type = FullyQualifiedName;

  } else {
    // Okay, we have a class, check if it is already parsed
    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(Param->getLocation(), DiagID) << FullyQualifiedName;
      return false;
    }

    // We export it.
    FuncParam.Type = AlreadyExported->second.ExportedName;
    FuncParam.RealType = AlreadyExported->second.Name;
  }
  return true;
}

bool hush::HushBindingMatcher::processPointerParam(
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {
  // Check if it is a pointer to a record
  clang::QualType PointerType = Param->getType();

  clang::QualType InnerType = PointerType->getPointeeType();
  while (InnerType->isPointerType()) {
    InnerType = InnerType->getPointeeType();
  }

  std::string FullyQualifiedName =
      PointerType.getCanonicalType().getAsString(D->getASTContext().getLangOpts());

  bool IsSpecialType = false;

  for (llvm::StringRef Namespace : SpecialNamespaces) {
    if (FullyQualifiedName.find(Namespace) != std::string::npos) {
      IsSpecialType = true;
      break;
    }
  }

  if (IsSpecialType) {
    // Okay, we have a class, check if it is already parsed
    std::string InnerFullyQualifiedName = InnerType.getAsString();
    auto AlreadyExported = this->ParsedClasses.find(InnerFullyQualifiedName);

    if (AlreadyExported == this->ParsedClasses.end()) {
     unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(Param->getLocation(), DiagID) << InnerFullyQualifiedName;
      return false;
    }

    FuncParam.RealType = FullyQualifiedName;
    FuncParam.Type = FullyQualifiedName;

    FuncParam.Type.replace(
        FuncParam.Type.find(InnerFullyQualifiedName), InnerFullyQualifiedName.size(),
        AlreadyExported->second.ExportedName);
  } else {
    FuncParam.Type = FullyQualifiedName;
    FuncParam.RealType = FullyQualifiedName;

    std::replace(FuncParam.Type.begin(), FuncParam.Type.end(), ':', '_');
  }

  FuncParam.Name = Param->getName();
  FuncParam.IsPointer = true;

  return true;
}
bool hush::HushBindingMatcher::processReferenceParam(
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {

  // Check if it is a reference to a record
  clang::QualType ReferenceType = Param->getType();

  // Check the reference type (lvalue or rvalue)
  if (!ReferenceType->isLValueReferenceType()) {
    // We only support lvalue references
    unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Error: %0 is a reference to a rvalue, it is currently not "
        "supported");

    clang::DiagnosticsEngine &DiagEngine = D->getASTContext().getDiagnostics();
    DiagEngine.Report(Param->getLocation(), DiagID)
        << ReferenceType.getAsString();

    return false;
  }
  // Okay, check the inner type
  clang::QualType InnerType = ReferenceType->getPointeeType();
  std::string InnerTypeName =
      InnerType.getAsString(D->getASTContext().getLangOpts());

  if (InnerType->isPointerType()) {
    unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Error: %0 is a reference to a pointer, it is currently not "
        "supported");

    clang::DiagnosticsEngine &DiagEngine =
        D->getASTContext().getDiagnostics();

    DiagEngine.Report(Param->getLocation(), DiagID) << InnerTypeName;

    return false;
  }


  std::string FullyQualifiedName =
      ReferenceType.getCanonicalType().getAsString(D->getASTContext().getLangOpts());

  bool IsSpecialType = false;

  for (llvm::StringRef Namespace : SpecialNamespaces) {
    if (FullyQualifiedName.find(Namespace) != std::string::npos) {
      IsSpecialType = true;
      break;
    }
  }

  if (IsSpecialType) {
    // Okay, we have a class, check if it is already parsed
    std::string InnerFullyQualifiedName = InnerType.getAsString();
    auto AlreadyExported = this->ParsedClasses.find(InnerFullyQualifiedName);

    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
           clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(Param->getLocation(), DiagID) << InnerFullyQualifiedName;
      return false;
    }

    FuncParam.RealType = FullyQualifiedName;
    FuncParam.Type = FullyQualifiedName;

    FuncParam.Type.replace(
        FuncParam.Type.find(InnerFullyQualifiedName), InnerFullyQualifiedName.size(),
        AlreadyExported->second.ExportedName);
  } else {
    FuncParam.Type = FullyQualifiedName;
    FuncParam.RealType = FullyQualifiedName;

    std::replace(FuncParam.Type.begin(), FuncParam.Type.end(), ':', '_');
  }

  FuncParam.Type.replace(FuncParam.Type.find('&'), 1, "*");

  FuncParam.Name = Param->getName();
  FuncParam.IsReference = true;


  return true;
}

FieldOptions getMemberFieldOptions(const clang::FieldDecl *Field) {
  // We must check if it has a HushExport attribute
  auto *HushExportAttr = Field->getAttr<clang::HushExportAttr>();
  bool Ignore = false;
  std::string ExportedName = Field->getName().str();

  if (HushExportAttr != nullptr) {
    for (auto *ArgExpr : HushExportAttr->exportConfig()) {
      // Check if the argument is a reference to a variable called ignore in
      // nms Hush::Export
      clang::DeclRefExpr *ArgRef =
          dyn_cast<clang::DeclRefExpr>(ArgExpr->IgnoreImplicit());

      if (ArgRef != nullptr) {
        if (isHushExportIgnore(ArgRef, Field->getASTContext())) {
          Ignore = true;
        }
      }

      // If it is a function call to Hush::Export::name, we should replace the
      // name of the class with the name of the function
      clang::CallExpr *ArgCall =
          dyn_cast<clang::CallExpr>(ArgExpr->IgnoreImplicit());
      if (ArgCall != nullptr) {
        if (auto Name = getHushExportName(ArgCall, Field->getASTContext());
            Name.has_value()) {
          ExportedName = *Name;
        }
      }
    }
  }

  return {Ignore, ExportedName};
}
