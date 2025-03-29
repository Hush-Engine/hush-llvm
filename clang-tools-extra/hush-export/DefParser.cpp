
#include "DefParser.h"
#include "ParserCommon.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ODRDiagsEmitter.h>
#include <clang/Basic/SourceManager.h>

using namespace clang;

struct FieldOptions {
  bool Ignore;
  std::string Name;
};

const std::set<std::string> SpecialTypes = {"glm::vec", "glm::mat", "glm::quat",
                                            "glm::transform", "glm::color"};

const std::array<std::string, 1> SpecialNamespaces = {"glm"};

bool isPOD(const clang::RecordDecl *D) {
  const auto &Context = D->getASTContext();
  clang::QualType RecordType = Context.getRecordType(D);

  // Check if the type is a POD type
  return RecordType.isPODType(Context);
}

void processSpecialTypeDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::QualType D);

void processHushExportClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D);

void processHushExportDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D);

void processPointerDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    ClassMemberVariable &Member, const clang::FieldDecl *D);

FieldOptions getMemberFieldOptions(const clang::FieldDecl *Field);

void processClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
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

  processHushExportClassDecl(ParsedClasses, ParsedClassesMap, HushExportAttr,
                             D);
}

void processEnumDecl(std::vector<std::shared_ptr<EnumDeclaration>> &ParsedEnum,
                     std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
                     const clang::HushExportAttr *HushExportAttr,
                     const clang::EnumDecl *D) {

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
    auto name = EnumConstant->getName().str();
    const int64_t value = EnumConstant->getInitVal().getExtValue();
    EnumData->EnumValues.push_back({name, value});
  }

  // Add the enum to the parsed enums
  ParsedEnum.push_back(EnumData);
  ParsedClassesMap[EnumName] = ExportedTypeData;
}

void processSpecialTypeDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::QualType D) {
  // Check if the type is in the glm namespace
  std::string FullTypeName = D.getAsString();
  std::string ExposedTypeName = FullTypeName;

  if (ParsedClassesMap.find(FullTypeName) != ParsedClassesMap.end()) {
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
      DiagnosticsEngine &DiagEngine = RD->getASTContext().getDiagnostics();
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
    ArrayRef<TemplateArgument> TemplateArgs =
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

    ParsedClassesMap[FullTypeName] = ExportedTypeData;
    ParsedClasses.push_back(ExportedClassInfo);
  }
}

void processHushExportClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
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
  if (ParsedClassesMap.find(FullyQualifiedName) != ParsedClassesMap.end()) {
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

    ParsedClassesMap[FullyQualifiedName] = ExportedTypeData;
    ParsedClasses.push_back(NewClass);
    return;
  }

  // Check if it is a POD type
  if (!isPOD(D)) {
    unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Class %0 is not a POD type, consider exporting it as a handle");

    DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
    DiagEngine.Report(D->getLocation(), DiagID) << D->getName();
    return;
  }

  auto NewClass = std::make_shared<ExportedClass>();
  auto ExportedTypeData = ExportedTypeInfo{};

  ExportedTypeData.Name = FullyQualifiedName;
  ExportedTypeData.ExportedName = ExportedName;
  ExportedTypeData.TypeData = NewClass;
  NewClass->Name = FullyQualifiedName;
  NewClass->ExportedName = ExportedName;

  // Process each one of the fields
  for (const auto *Field : D->fields()) {
    // Get the field type
    const clang::QualType FieldType = Field->getType();

    // Check if the field is private or protected, if it is, issue a warning
    if (Field->getAccess() != clang::AccessSpecifier::AS_public) {

      // Create a random name for the field

      // Private members should be exported as a char buffer with specific alignment
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
        processSpecialTypeDecl(ParsedClasses, ParsedClassesMap,
                               Field->getType());
        break;
      }
    }

    FieldOptions fieldOptions = getMemberFieldOptions(Field);

    if (fieldOptions.Ignore) {
      // Issue an error
      unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error,
          "Field %0 is ignored, this is not supported when generating the "
          "bindings");

      DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
      DiagEngine.Report(Field->getLocation(), DiagID) << Field->getName();

      return;
    }

    auto NewField = ClassMemberVariable{};
    NewField.Name = fieldOptions.Name;
    NewField.Alignment = Context.getTypeAlign(FieldType);
    NewField.Size = Context.getTypeSize(FieldType);

    // Okay, check if it is a record decl
    if (FieldType->isBuiltinType()) {
      NewField.Type = FieldType.getAsString();
    } else if (FieldType->isRecordType()) {
      // Get the field fully qualified name
      std::string FieldQualifiedName = Field->getType().getAsString();

      // Okay, we need to check if the field is already parsed
      auto AlreadyExported = ParsedClassesMap.find(FieldQualifiedName);
      if (AlreadyExported == ParsedClassesMap.end()) {
        unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Field %0 is of type %1, which is not exported");

        DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
        DiagEngine.Report(Field->getLocation(), DiagID)
            << Field->getName() << FieldQualifiedName;
        return;
      }

      // Export the field
      NewField.Type = AlreadyExported->second.ExportedName;
    } else if (FieldType->isPointerType()) {
      processPointerDecl(ParsedClasses, ParsedClassesMap, NewField, Field);
    }

    NewClass->Members.push_back(NewField);
  }

  ParsedClassesMap.insert(std::make_pair(FullyQualifiedName, ExportedTypeData));
  ParsedClasses.push_back(NewClass);
}

void processPointerDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    ClassMemberVariable &Member, const clang::FieldDecl *D) {

  // Get the pointee type
  const clang::QualType PointeeType = D->getType()->getPointeeType();

  if (PointeeType->isBuiltinType()) {
    Member.Type = PointeeType.getAsString();
    Member.IsPointer = true;
    return;
  }

  // Not a built-in type, it must be an enum or a class
  if (PointeeType->isEnumeralType()) {
    // Check if we have the enum already parsed
    auto AlreadyExported = ParsedClassesMap.find(PointeeType.getAsString());
    if (AlreadyExported == ParsedClassesMap.end()) {
      // Try to parse it
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();

      DiagEngine.Report(D->getLocation(), DiagID) << PointeeType.getAsString();
      return;
    }

    // Export the pointer
    Member.IsPointer = true;
    Member.Type = AlreadyExported->second.ExportedName;
    return;
  }

  // It must be a record
  const clang::RecordDecl *RecordDecl = PointeeType->getAsRecordDecl();
  if (RecordDecl == nullptr) {
    unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

    DiagnosticsEngine &DiagEngine = D->getASTContext().getDiagnostics();

    DiagEngine.Report(D->getLocation(), DiagID);
    return;
  }

  // First, check if the pointer is of a built-in type.
  auto FullyQualifiedName = RecordDecl->getQualifiedNameAsString();

  // If the type is a special type or a built-in type, we should export as-is
  for (const auto &SpecialType : SpecialTypes) {
    if (FullyQualifiedName.find(SpecialType) != std::string::npos) {
      Member.Type = FullyQualifiedName;
      Member.IsPointer = true;
      return;
    }
  }

  // Okay, we have a class, check if it is already parsed
  auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
  if (AlreadyExported == ParsedClassesMap.end()) {
    // Check if it has a HushExport attribute
    auto *HushExportAttr = RecordDecl->getAttr<clang::HushExportAttr>();
    if (HushExportAttr == nullptr) {
      unsigned DiagID =
          RecordDecl->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");
      DiagnosticsEngine &DiagEngine =
          RecordDecl->getASTContext().getDiagnostics();

      DiagEngine.Report(RecordDecl->getLocation(), DiagID)
          << FullyQualifiedName;
      return;
    }

    processHushExportClassDecl(ParsedClasses, ParsedClassesMap, HushExportAttr,
                               RecordDecl);

    // Find it again
    AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
    if (AlreadyExported == ParsedClassesMap.end()) {
      unsigned DiagID =
          RecordDecl->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      DiagnosticsEngine &DiagEngine =
          RecordDecl->getASTContext().getDiagnostics();

      DiagEngine.Report(RecordDecl->getLocation(), DiagID);
      return;
    }
  }

  // Okay, export the pointer
  Member.IsPointer = true;
  Member.Type = AlreadyExported->second.ExportedName;
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
