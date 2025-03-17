//
// Created by Alan5 on 28/02/2025.
//

#ifndef FUNCTIONPARSER_H
#define FUNCTIONPARSER_H

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <set>
#include "DefParser.h"

void processFunctionDecl(std::map<std::string, ExportedClassInfo> &ParsedClasses,
                         const clang::HushExportAttr *HushExportAttr,
                         const clang::FunctionDecl *D,
                         llvm::raw_ostream &OutHeaderFile,
                         llvm::raw_ostream &OutCppFile,
                         llvm::raw_ostream &OutPrototypeFile);

#endif // FUNCTIONPARSER_H
