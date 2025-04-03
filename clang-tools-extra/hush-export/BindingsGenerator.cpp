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
  }

  // Get HushExport attribute, it should be one child
  if (const clang::FunctionDecl *FD =
          Result.Nodes.getNodeAs<clang::FunctionDecl>("hushExportable")) {
    clang::HushExportAttr *HushExportAttr =
        FD->getAttr<clang::HushExportAttr>();
    processFunctionDecl(HushExportAttr, FD);
  }

  if (const clang::EnumDecl *ED =
          Result.Nodes.getNodeAs<clang::EnumDecl>("hushExportable")) {
    clang::HushExportAttr *HushExportAttr =
        ED->getAttr<clang::HushExportAttr>();
    processEnumDecl(HushExportAttr, ED);
  }
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
    FuncParam.IsConst = Param->getType().isConstQualified();
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

        if (ArgRef != nullptr) {
          if (isHushExportIgnore(ArgRef, D->getASTContext())) {
            // Ignore this parameter.
            continue;
          }
        }

        clang::CallExpr *ArgCall =
            llvm::dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
        if (ArgCall != nullptr) {
          if (auto Name = getHushExportName(ArgCall, D->getASTContext());
              Name.has_value()) {
            FuncParam.Name = *Name;
          }
        }
      }
    }

    if (Param->getType()->isRecordType()) {
      if (!processRecordTypeParam(D, Param, FuncParam))
        return;
    } else if (Param->getType()->isPointerType() ||
               Param->getType()->isReferenceType()) {
      if (!processPointerParam(D, Param, FuncParam)) {
        return;
      }
    } else if (Param->getType()->isBuiltinType()) {
      FuncParam.Type = Param->getType().getAsString();
    } else if (Param->getType()->isEnumeralType()) {
      auto ParamType = Param->getType();

      // Check if ReturnType is a ElaboratedType, if it is, we need to get the
      // named type, which is the actual enum.
      if (auto *ElabType = ParamType->getAs<clang::ElaboratedType>()) {
        ParamType = ElabType->getNamedType();
      }

      auto FullyQualifiedName = ParamType.getAsString();

      if (FullyQualifiedName.find("enum ") != std::string::npos) {
        FullyQualifiedName.erase(0, 5);
      }

      auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
      if (AlreadyExported == this->ParsedClasses.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(Param->getLocation(), DiagID) << FullyQualifiedName;
        return;
      }

      FuncParam.Type = AlreadyExported->second.ExportedName;
      FuncParam.RealType = AlreadyExported->second.Name;
      FuncParam.EnumType = FullyQualifiedName;
    }

    // Add the parameter
    FuncInfo.Parameters.push_back(FuncParam);
  }

  this->Functions.push_back(FuncInfo);
}

void hush::HushBindingMatcher::processEnumDecl(
    const clang::HushExportAttr *HushExportAttr, const clang::EnumDecl *D) {}

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
    } else if (const auto matPos = ExposedTypeName.find("mat");
               matPos != std::string::npos) {
      TypeTemplateIndex = 2;
      ExposedTypeName.replace(ExposedTypeName.find("mat"), 3, "Matrix");
    } else if (auto quatPos = ExposedTypeName.find("quat");
               quatPos != std::string::npos) {
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
      clang::QualType qualifiedType = Field->getType();

      // std::string FieldTypeStr = qualifiedType.getAsString();

      auto MemberVariable = ClassMemberVariable{};
      MemberVariable.Name = Field->getName().str();
      MemberVariable.Type = FieldTypeStr;
      MemberVariable.Alignment =
          RD->getASTContext().getTypeAlign(qualifiedType);
      MemberVariable.Size = RD->getASTContext().getTypeSize(qualifiedType);

      ExportedClassInfo->Members.push_back(MemberVariable);
    }

    ExportedTypeData.TypeData = ExportedClassInfo;

    this->ParsedClasses.insert({FullTypeName, ExportedTypeData});
    this->ParsedClassesVector.push_back(ExportedClassInfo);
  }
}

bool hush::HushBindingMatcher::processPointerDecl(
    const clang::RecordDecl *D, clang::ASTContext &Context,
    const clang::FieldDecl *Field, const clang::QualType FieldType,
    hush::ClassMemberVariable &NewField) {
  const clang::QualType PointeeType = FieldType->getPointeeType();
  clang::QualType InnerType = PointeeType;
  std::string InnerTypeName;
  std::string PointerFullyQualifiedName =
      FieldType.getAsString(D->getASTContext().getPrintingPolicy());

  while (InnerType->isPointerType()) {
    InnerType = InnerType->getPointeeType();
  }

  // Get the inner type name withou const, volatile, etc.
  const clang::QualType InnerTypeNoCV =
      InnerType.getUnqualifiedType();
  InnerTypeName = InnerTypeNoCV.getAsString(D->getASTContext().getPrintingPolicy());

  if (InnerType->isRecordType()) {
    // Okay, it is a record, check if it is already parsed
    std::string FullyQualifiedName =
        InnerType->getAsRecordDecl()->getQualifiedNameAsString();
    std::string FieldTypeName = FieldType.getAsString(Context.getLangOpts());
    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);

    if (AlreadyExported == this->ParsedClasses.end()) {
      // Check if it is part of the special namespaces
      for (const auto &Namespace : SpecialNamespaces) {
        if (FullyQualifiedName.find(Namespace) != std::string::npos) {
          processSpecialTypeDecl(Field->getType());
          break;
        }
      }

      // Okay, a normal type, check if it has a HushExport attribute
      auto *HushExportAttr =
          InnerType->getAsRecordDecl()->getAttr<clang::HushExportAttr>();

      if (HushExportAttr == nullptr) {
        unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Field %0 is of type %1, which is not exported");

        clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
        DiagEngine.Report(Field->getLocation(), DiagID)
            << Field->getName() << Field->getType();
        return false;
      }
      // Process the class
      processHushExportClassDecl(HushExportAttr, InnerType->getAsRecordDecl());
    }
    // Find it again
    AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);

    // Get the exported name
    std::string ExportedPointerInnerName = AlreadyExported->second.ExportedName;

    // Replace the real type with the exported name
    PointerFullyQualifiedName.replace(
        PointerFullyQualifiedName.find(InnerTypeName), InnerTypeName.size(),
        ExportedPointerInnerName);

    NewField.Type = PointerFullyQualifiedName;
  } else if (InnerType->isBuiltinType()) {
    // Just export the type by substituting the type
    std::string FieldTypeStr = InnerType.getAsString(Context.getLangOpts());
    std::string PointerTypeStr =
        FieldType->getPointeeType().getAsString(Context.getLangOpts());

    PointerTypeStr.replace(PointerTypeStr.find(FieldTypeStr),
                           FieldTypeStr.size(), FieldTypeStr);

    NewField.Type = PointerTypeStr;
  } else {
    unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Field %0 is of type %1, which is not exported");

    clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
    DiagEngine.Report(Field->getLocation(), DiagID)
        << Field->getName() << Field->getType();
    return false;
  }

  return true;
}
bool hush::HushBindingMatcher::processFieldEnum(
    clang::ASTContext &Context, const clang::FieldDecl *Field,
    const clang::QualType FieldType) {
  // Get the enum decl
  const clang::TagDecl *TagDecl = FieldType->getAsTagDecl();
  const clang::EnumDecl *EnumDecl = llvm::dyn_cast<clang::EnumDecl>(TagDecl);

  if (EnumDecl == nullptr) {
    unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Field %0 is of type %1, which is not exported");

    clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
    DiagEngine.Report(Field->getLocation(), DiagID)
        << Field->getName() << FieldType.getAsString(Context.getLangOpts());
    return false;
  }

  // Get the hush export attribute
  auto *EnumHushExportAttr = EnumDecl->getAttr<clang::HushExportAttr>();
  if (EnumHushExportAttr == nullptr) {
    unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Field %0 is of type %1, which is not exported");

    clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
    DiagEngine.Report(Field->getLocation(), DiagID)
        << Field->getName() << FieldType.getAsString(Context.getLangOpts());
    return false;
  }

  processEnumDecl(EnumHushExportAttr, EnumDecl);
  return true;
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

      // If it is a function call to Hush::Export::name, we should replace the
      // name of the class with the name of the function
      clang::CallExpr *ArgCall =
          dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
      if (ArgCall != nullptr) {
        if (auto Name = getHushExportName(ArgCall, Context); Name.has_value()) {
          ExportedName = *Name;
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

  // Check if it is a POD type
  // if (!isPOD(D)) {
  //   unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
  //       clang::DiagnosticsEngine::Error,
  //       "Class %0 is not a POD type, consider exporting it as a handle");
  //
  //   DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
  //   DiagEngine.Report(D->getLocation(), DiagID) << D->getName();
  //   return;
  // }

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
    for (const auto &Namespace : SpecialNamespaces) {
      if (FieldTypeStr.find(Namespace) != std::string::npos) {
        processSpecialTypeDecl(Field->getType());
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

    // Okay, check if it is a record decl
    if (FieldType->isBuiltinType()) {
      NewField.Type = FieldType.getAsString();
    } else if (FieldType->isRecordType()) {
      // Get the field fully qualified name
      std::string FieldQualifiedName = FieldType.getAsString();

      // Get also the record declaration
      const clang::RecordDecl *RecordDecl = FieldType->getAsRecordDecl();

      if (RecordDecl == nullptr) {
        unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Field %0 is of type %1, which is not exported");

        clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
        DiagEngine.Report(Field->getLocation(), DiagID)
            << Field->getName() << FieldQualifiedName;
        return;
      }

      std::string FieldFullyQualifiedName =
          RecordDecl->getQualifiedNameAsString();

      // Okay, we need to check if the field is already parsed
      auto AlreadyExported = this->ParsedClasses.find(FieldFullyQualifiedName);
      if (AlreadyExported == this->ParsedClasses.end()) {
        unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Field %0 is of type %1, which is not exported");

        clang::DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
        DiagEngine.Report(Field->getLocation(), DiagID)
            << Field->getName() << FieldQualifiedName;
        return;
      }

      // Export the field
      NewField.Type = AlreadyExported->second.ExportedName;
    } else if (FieldType->isPointerType()) {
      if (!processPointerDecl(D, Context, Field, FieldType, NewField))
        return;
    } else if (FieldType->isEnumeralType()) {
      if (!processFieldEnum(Context, Field, FieldType))
        return;
    }

    NewClass->Members.push_back(NewField);
  }

  this->ParsedClasses.insert(
      std::make_pair(FullyQualifiedName, ExportedTypeData));
  this->ParsedClassesVector.push_back(NewClass);
}

void hush::HushBindingMatcher::processPointerDecl(ClassMemberVariable &Member,
                                                  const clang::FieldDecl *D) {

  // Get the pointee type
  const clang::QualType PointeeType = D->getType()->getPointeeType();

  if (PointeeType->isBuiltinType()) {
    Member.Type = PointeeType.getAsString();
    return;
  }

  // Not a built-in type, it must be an enum or a class
  if (PointeeType->isEnumeralType()) {
    // Check if we have the enum already parsed
    auto AlreadyExported = this->ParsedClasses.find(PointeeType.getAsString());
    if (AlreadyExported == this->ParsedClasses.end()) {
      // Try to parse it
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();

      DiagEngine.Report(D->getLocation(), DiagID) << PointeeType.getAsString();
      return;
    }

    // Export the pointer
    Member.Type = AlreadyExported->second.ExportedName;
    return;
  }

  // It must be a record
  const clang::RecordDecl *RecordDecl = PointeeType->getAsRecordDecl();
  if (RecordDecl == nullptr) {
    unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

    clang::DiagnosticsEngine &DiagEngine = D->getASTContext().getDiagnostics();

    DiagEngine.Report(D->getLocation(), DiagID);
    return;
  }

  // First, check if the pointer is of a built-in type.
  auto FullyQualifiedName = RecordDecl->getQualifiedNameAsString();

  // If the type is a special type or a built-in type, we should export as-is
  for (const auto &SpecialType : SpecialTypes) {
    if (FullyQualifiedName.find(SpecialType) != std::string::npos) {
      Member.Type = FullyQualifiedName;
      return;
    }
  }

  // Okay, we have a class, check if it is already parsed
  auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
  if (AlreadyExported == this->ParsedClasses.end()) {
    // Check if it has a HushExport attribute
    auto *HushExportAttr = RecordDecl->getAttr<clang::HushExportAttr>();
    if (HushExportAttr == nullptr) {
      unsigned DiagID =
          RecordDecl->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");
      clang::DiagnosticsEngine &DiagEngine =
          RecordDecl->getASTContext().getDiagnostics();

      DiagEngine.Report(RecordDecl->getLocation(), DiagID)
          << FullyQualifiedName;
      return;
    }

    processHushExportClassDecl(HushExportAttr, RecordDecl);

    // Find it again
    AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID =
          RecordDecl->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          RecordDecl->getASTContext().getDiagnostics();

      DiagEngine.Report(RecordDecl->getLocation(), DiagID);
      return;
    }
  }

  // Okay, export the pointer
  Member.Type = AlreadyExported->second.ExportedName;
}
bool hush::HushBindingMatcher::processPointerRet(
    const clang::FunctionDecl *FunctionDeclInfo, ReturnTypeInfo &ReturnType,
    const clang::QualType PointeeType) {
  // Check if the pointee is a record
  if (PointeeType->isRecordType()) {
    clang::QualType RecordType = PointeeType;
    // Check if it as an elaborated type
    if (auto *ElabType = PointeeType->getAs<clang::ElaboratedType>()) {
      RecordType = ElabType->getNamedType();
    }

    auto FullyQualifiedName = RecordType.getAsString();

    // Check if it has struct or class and remove it
    if (FullyQualifiedName.find("struct ") != std::string::npos) {
      FullyQualifiedName.erase(0, 7);
    } else if (FullyQualifiedName.find("class ") != std::string::npos) {
      FullyQualifiedName.erase(0, 6);
    }

    // Okay, we have a class, check if it is already parsed
    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID =
          FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          FunctionDeclInfo->getASTContext().getDiagnostics();
      DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
          << FullyQualifiedName;
      return false;
    }

    // We export it.
    ReturnType.Type = AlreadyExported->second.ExportedName + "*";
  } else if (PointeeType->isBuiltinType()) {
    // We don't need to do anything
    ReturnType.Type = PointeeType.getAsString() + "*";
  } else if (PointeeType->isEnumeralType()) {
    ReturnType.Type = PointeeType.getAsString() + "*";
  } else if (PointeeType->isPointerType()) {
    unsigned DiagID =
        FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Error: %0 is a pointer to a pointer, it is currently not "
            "supported");

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
    FuncInfo.ReturnType.Type = ReturnType.getAsString();
  } else if (ReturnType->isRecordType()) {
    auto FullyQualifiedName = ReturnType.getAsString();

    // First, check if it as a special type
    if (FullyQualifiedName.find("std::span") != std::string::npos ||
        FullyQualifiedName.find("std::string_view") != std::string::npos ||
        FullyQualifiedName.find("std::vector") != std::string::npos ||
        FullyQualifiedName.find("std::string") != std::string::npos) {
      // Get the inner type
      auto InnerType = ReturnType->getAs<clang::TemplateSpecializationType>()
                           ->template_arguments()
                           .front()
                           .getAsType();

      // If the inner type is a record, we need to check if it is already
      // parsed
      if (InnerType->isRecordType()) {
        auto InnerFullyQualifiedName = InnerType.getAsString();

        // Okay, we have a class, check if it is already parsed
        auto AlreadyExported =
            this->ParsedClasses.find(InnerFullyQualifiedName);
        if (AlreadyExported == this->ParsedClasses.end()) {
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(D->getLocation(), DiagID)
              << InnerFullyQualifiedName;
          return false;
        }

        FuncInfo.ReturnType.InnerType = AlreadyExported->second.ExportedName;
      } else {
        FuncInfo.ReturnType.InnerType = InnerType.getAsString();
      }

      // Remove everything after the first <
      auto Pos = FullyQualifiedName.find('<');
      if (Pos != std::string::npos) {
        FullyQualifiedName.erase(Pos);
      }

      FuncInfo.ReturnType.Type = FullyQualifiedName;
    } else {
      // Okay, we have a class, check if it is already parsed
      auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
      if (AlreadyExported == this->ParsedClasses.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
        return false;
      }

      // We export it.
      FuncInfo.ReturnType.Type = AlreadyExported->second.ExportedName;
    }
  } else if (ReturnType->isEnumeralType()) {

    // Check if ReturnType is a ElaboratedType, if it is, we need to get the
    // named type, which is the actual enum.
    if (auto *ElabType = ReturnType->getAs<clang::ElaboratedType>()) {
      ReturnType = ElabType->getNamedType();
    }

    // Check if it is already parsed
    auto FullyQualifiedName = ReturnType.getAsString();

    if (FullyQualifiedName.find("enum ") != std::string::npos) {
      FullyQualifiedName.erase(0, 5);
    }

    auto AlreadyExported = this->ParsedClasses.find(FullyQualifiedName);
    if (AlreadyExported == this->ParsedClasses.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
      return false;
    }

    // We export it.
    FuncInfo.ReturnType.Type = AlreadyExported->second.ExportedName;
    FuncInfo.ReturnType.IsEnum = true;
  } else if (ReturnType->isPointerType() || ReturnType->isReferenceType()) {
    auto PointeeType = ReturnType->getPointeeType();

    FuncInfo.ReturnType.IsReference = ReturnType->isReferenceType();

    if (!processPointerRet(D, FuncInfo.ReturnType, PointeeType)) {
      return false;
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
    auto InnerType = Param->getType()
                         ->getAs<clang::TemplateSpecializationType>()
                         ->template_arguments()
                         .front()
                         .getAsType();

    // If the inner type is a record, we need to check if it is already parsed
    if (InnerType->isRecordType()) {
      auto InnerFullyQualifiedName = InnerType.getAsString();

      // Okay, we have a class, check if it is already parsed
      auto AlreadyExported = this->ParsedClasses.find(InnerFullyQualifiedName);
      if (AlreadyExported == this->ParsedClasses.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(Param->getLocation(), DiagID)
            << InnerFullyQualifiedName;
        return false;
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
  auto PointeeType = Param->getType()->getPointeeType();

  // If the pointee is a record, we need to check if it is already parsed
  if (PointeeType->isRecordType()) {
    // Check if it is elaborated
    if (const auto *Elaborated = PointeeType->getAs<clang::ElaboratedType>();
        Elaborated != nullptr) {
      PointeeType = Elaborated->getNamedType();
    }

    auto FullyQualifiedName = PointeeType.getAsString();

    if (FullyQualifiedName.find("struct ") != std::string::npos) {
      FullyQualifiedName = FullyQualifiedName.substr(7);
    } else if (FullyQualifiedName.find("class ") != std::string::npos) {
      FullyQualifiedName = FullyQualifiedName.substr(6);
    }

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

    FuncParam.Type = AlreadyExported->second.ExportedName;
    FuncParam.RealType = AlreadyExported->second.Name;
  } else if (PointeeType->isBuiltinType()) {
    FuncParam.Type = PointeeType.getAsString();
  }

  return true;
}
std::optional<std::string> hush::HushBindingMatcher::registerTypeIfNotExported(
    const clang::QualType &Type) {
  // First, check if the type is already exported
  auto AlreadyExported = this->ParsedClasses.find(Type.getAsString());
  if (AlreadyExported != this->ParsedClasses.end()) {
    return AlreadyExported->second.ExportedName;
  }

  // Okay, not exported, we need to check if it is a record
  if (Type->isRecordType()) {
    // Get the record declaration
    const clang::RecordDecl *RD = Type->getAsRecordDecl();

    // Check if it has a HushExport attribute
    auto *HushExportAttr = RD->getAttr<clang::HushExportAttr>();
    if (HushExportAttr == nullptr) {
      return std::nullopt;
    }

    processHushExportClassDecl(HushExportAttr, RD);
  } else if (Type->isPointerType()) {
    // Check if it is a pointer to a record
    const clang::QualType PointeeType = Type->getPointeeType();
    if (PointeeType->isRecordType()) {
      // Get the record declaration
      const clang::RecordDecl *RD = PointeeType->getAsRecordDecl();

      // Check if it has a HushExport attribute
      auto *HushExportAttr = RD->getAttr<clang::HushExportAttr>();
      if (HushExportAttr == nullptr) {
        return std::nullopt;
      }

      processHushExportClassDecl(HushExportAttr, RD);
    } else if (PointeeType->isBuiltinType()) {
      // We don't need to do anything
    } else if (PointeeType->isEnumeralType()) {
      // Check if it is already parsed
      auto FullyQualifiedName = PointeeType.getAsString();

      if (FullyQualifiedName.find("enum ") != std::string::npos) {
        FullyQualifiedName.erase(0, 5);
      }
    }
  }
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
