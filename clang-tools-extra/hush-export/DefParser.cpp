
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
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::QualType D);

void processHushExportClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D);

void processHushExportDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::HushExportAttr *HushExportAttr, const clang::RecordDecl *D);

void processPointerDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    ClassMemberVariable &Member, const clang::RecordDecl *D);

FieldOptions getMemberFieldOptions(const clang::FieldDecl *Field);

void processClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
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

void processSpecialTypeDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
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

    std::shared_ptr<ExportedClass> ExportedClassPtr =
        std::make_shared<ExportedClass>();

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
    ExportedClassPtr->Name = FullTypeName;
    ExportedClassPtr->ExportedName = ExposedTypeName;
    ExportedClassPtr->IsHandle = false;

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

      ExportedClassPtr->Members.push_back(MemberVariable);
    }

    ParsedClassesMap[FullTypeName] = ExportedClassPtr;
    ParsedClasses.push_back(ExportedClassPtr);
  }
}

void processHushExportClassDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
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
    NewClass->Name = FullyQualifiedName;
    NewClass->ExportedName = ExportedName;
    NewClass->IsHandle = true;

    ParsedClassesMap[FullyQualifiedName] = NewClass;
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
  NewClass->Name = FullyQualifiedName;
  NewClass->ExportedName = ExportedName;

  // Process each one of the fields
  for (const auto *Field : D->fields()) {
    // Get the field type
    const clang::QualType FieldType = Field->getType();

    // Check if the field is private or protected, if it is, issue a warning
    if (Field->getAccess() != clang::AccessSpecifier::AS_public) {
      unsigned int DiagID = Context.getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Warning,
          "Field %0 is not public, consider not exporting it or exporting the "
          "class as a handle");

      DiagnosticsEngine &DiagEngine = Context.getDiagnostics();
      DiagEngine.Report(Field->getLocation(), DiagID) << Field->getName();
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
      NewField.Type = AlreadyExported->second->ExportedName;

    } else if (FieldType->isPointerType()) {
      processPointerDecl(ParsedClasses, ParsedClassesMap, NewField,
                         Field->getType()->getPointeeType()->getAsRecordDecl());
    }

    NewClass->Members.push_back(NewField);
  }

  ParsedClassesMap.insert(std::make_pair(FullyQualifiedName, NewClass));
  ParsedClasses.push_back(NewClass);
}

void processPointerDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    ClassMemberVariable &Member, const clang::RecordDecl *D) {
  // First, check if the pointer is of a built-in type.
  const clang::QualType RecordType = D->getASTContext().getRecordType(D);

  // If the type is a special type or a built-in type, we should export as-is
  if (SpecialTypes.find(RecordType.getAsString()) != SpecialTypes.end() ||
      RecordType->isBuiltinType()) {
    Member.Type = RecordType.getAsString();
    Member.IsPointer = true;
  }

  // We don't need the class part, we only need the name of it.
  auto FullyQualifiedName = D->getQualifiedNameAsString();

  // Okay, we have a class, check if it is already parsed
  auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
  if (AlreadyExported == ParsedClassesMap.end()) {
    // Check if it has a HushExport attribute
    auto *HushExportAttr = D->getAttr<clang::HushExportAttr>();
    if (HushExportAttr == nullptr) {
      // Issue an error
      llvm::errs() << "Error: " << RecordType.getAsString()
                   << " is not exported\n";
      return;
    }

    processHushExportClassDecl(ParsedClasses, ParsedClassesMap, HushExportAttr,
                               D);

    // Find it again
    AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
    if (AlreadyExported == ParsedClassesMap.end()) {
      // Issue an error
      llvm::errs() << "Error: " << RecordType.getAsString()
                   << " is not exported\n";
      return;
    }
  }

  // Okay, export the pointer
  Member.IsPointer = true;
  Member.Type = AlreadyExported->second->ExportedName;
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