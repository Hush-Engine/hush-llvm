// Hush Engine

#ifndef DEFPARSER_H
#define DEFPARSER_H

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <set>

struct ExportedClassInfo {
  std::string ExportedName;
  bool IsTransparent;
};

void processClassDecl(std::map<std::string, ExportedClassInfo> &ParsedClasses,
                      const clang::HushExportAttr *HushExportAttr,
                      const clang::RecordDecl *D,
                      llvm::raw_ostream &OutHeaderFile,
                      llvm::raw_ostream &OutCppFile);

#endif // DEFPARSER_H
