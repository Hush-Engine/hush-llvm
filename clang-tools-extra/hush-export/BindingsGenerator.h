//
// Created by Alan5 on 01/04/2025.
//

#ifndef BINDINGSGENERATOR_H
#define BINDINGSGENERATOR_H

#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ExtractAPI/API.h>

#include <variant>

using namespace llvm;

namespace hush {

struct ClassMemberVariable {
  std::string Name;
  std::string Type;
  std::size_t Size = 0;
  std::size_t Alignment = 0;
  bool IsHidden = false;
};

struct ExportedClass {
  std::string Name;
  std::string ExportedName;
  bool IsHandle;
  std::vector<ClassMemberVariable> Members;
  std::string FileName;
  bool IsNonPOD = false;
};

struct EnumPairValue {
  std::string Name;
  int64_t Value;
};

struct EnumDeclaration {
  std::string Name;
  std::string ExportedName;
  std::string InnerType;
  std::vector<EnumPairValue> EnumValues;
  bool IsPlainEnum;
};

struct ExportedTypeInfo {
  std::string Name;
  std::string ExportedName;
  std::variant<std::shared_ptr<ExportedClass>, std::shared_ptr<EnumDeclaration>>
      TypeData;
};

struct InnerTypeInfo {
  std::string Type;
};

struct FunctionParam {
  std::string Name;
  std::string Type;
  std::string RealType;
  std::string EnumType;
  InnerTypeInfo InnerType; // Useful for span, string_view.
  bool IsPointer;
  bool IsReference;
};

struct ReturnTypeInfo {
  std::string Type;
  std::string InnerType; // Useful for span, string_view, vector,
  std::string Name;
  bool IsEnum = false;
  bool IsReference = false;
};

struct FunctionInfo {
  std::string Name;
  std::string ExportedName;
  ReturnTypeInfo ReturnType;
  std::vector<FunctionParam> Parameters;
  std::optional<std::shared_ptr<ExportedClass>> ContainingClass;
};

struct TypeDefInfo {
  std::string Name;
  std::string ExportedName;
};

class HushBindingMatcher
    : public clang::ast_matchers::MatchFinder::MatchCallback {
  std::map<std::string, ExportedTypeInfo> ParsedClasses;
  std::vector<std::shared_ptr<ExportedClass>> ParsedClassesVector;
  std::vector<std::shared_ptr<EnumDeclaration>> ParsedEnums;
  std::vector<FunctionInfo> Functions;
  std::vector<TypeDefInfo> Typedefs;

public:
  HushBindingMatcher() {
    addSpecialTypes();
  }

  void
  run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

  std::map<std::string, ExportedTypeInfo> &getParsedClasses() {
    return ParsedClasses;
  }

  std::vector<std::shared_ptr<ExportedClass>> &getParsedClassesVector() {
    return ParsedClassesVector;
  }

  std::vector<std::shared_ptr<EnumDeclaration>> &getParsedEnums() {
    return ParsedEnums;
  }

  std::vector<FunctionInfo> &getFunctions() { return Functions; }

private:

  void addSpecialTypes();

  void processClassDecl(const clang::HushExportAttr *HushExportAttr,
                        const clang::RecordDecl *D);

  void processFunctionDecl(const clang::HushExportAttr *HushExportAttr,
                           const clang::FunctionDecl *D);

  void processEnumDecl(const clang::HushExportAttr *HushExportAttr,
                       const clang::EnumDecl *D);

  // Helpers for exporting classes and enums
private:
  void processSpecialTypeDecl(const clang::QualType D);

  void processHushExportClassDecl(const clang::HushExportAttr *HushExportAttr,
                                  const clang::RecordDecl *D);

  // Helper functions for exporting functions
private:
  bool processPointerRet(const clang::FunctionDecl *FunctionDeclInfo,
                         ReturnTypeInfo &ReturnType);

  bool processFuncReturnType(const clang::FunctionDecl *D,
                             FunctionInfo &FuncInfo,
                             clang::QualType ReturnType);

  bool processRecordTypeParam(const clang::FunctionDecl *D,
                              const clang::ParmVarDecl *Param,
                              FunctionParam &FuncParam);

  bool processPointerParam(const clang::FunctionDecl *D,
                           const clang::ParmVarDecl *Param,
                           FunctionParam &FuncParam);

  bool processReferenceParam(const clang::FunctionDecl *D,
                             const clang::ParmVarDecl *Param,
                             FunctionParam &FunctionParam);

  std::optional<std::string>
  registerTypeIfNotExported(const clang::QualType &Type);
};
} // namespace hush

#endif // BINDINGSGENERATOR_H
