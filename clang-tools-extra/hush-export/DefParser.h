// Hush Engine

#ifndef DEFPARSER_H
#define DEFPARSER_H

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <variant>
#include <set>

struct ClassMemberVariable {
  std::string Name;
  std::string Type;
  std::size_t Size = 0;
  std::size_t Alignment = 0;
  bool IsPointer = false;
  bool IsHidden = false;
};

struct ExportedClass {
  std::string Name;
  std::string ExportedName;
  bool IsHandle;
  std::vector<ClassMemberVariable> Members;
  std::string FileName;
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
  std::variant<std::shared_ptr<ExportedClass>, std::shared_ptr<EnumDeclaration>> TypeData;
};

void processClassDecl(std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
                      std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
                      const clang::HushExportAttr *HushExportAttr,
                      const clang::RecordDecl *D);

void processEnumDecl(std::vector<std::shared_ptr<EnumDeclaration>> &ParsedEnum,
                     std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
                     const clang::HushExportAttr *HushExportAttr,
                     const clang::EnumDecl *D);

void processSpecialTypeDecl(
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::QualType D);


#endif // DEFPARSER_H
