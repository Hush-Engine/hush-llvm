// Hush Engine

#ifndef DEFPARSER_H
#define DEFPARSER_H

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <set>

struct ClassMemberVariable {
  std::string Name;
  std::string Type;
  std::size_t Size = 0;
  std::size_t Alignment = 0;
  bool IsPointer = false;
};

struct ExportedClass {
  std::string Name;
  bool IsHandle;
  std::vector<ClassMemberVariable> Members;
  std::string ExportedName;
  std::string FileName;
};

struct ExportedClassInfo {
  std::string ExportedName;
  bool IsTransparent;
};

void processClassDecl(std::vector<std::shared_ptr<ExportedClass>> &ParsedClasses,
                      std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
                      const clang::HushExportAttr *HushExportAttr,
                      const clang::RecordDecl *D);


#endif // DEFPARSER_H
