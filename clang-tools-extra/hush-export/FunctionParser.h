//
// Created by Alan5 on 28/02/2025.
//

#ifndef FUNCTIONPARSER_H
#define FUNCTIONPARSER_H

#include "DefParser.h"
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <set>

struct InnerTypeInfo {
  std::string Type;
  bool IsPointer;
  bool IsReference;
  bool IsConst;
};

struct FunctionParam {
  std::string Name;
  std::string Type;
  InnerTypeInfo InnerType; // Useful for span, string_view.
  bool IsPointer;
  bool IsReference;
  bool IsConst;
};

struct ReturnTypeInfo {
  std::string Type;
  std::string InnerType; // Useful for span, string_view, vector,
};

struct FunctionInfo {
  std::string Name;
  std::string ExportedName;
  ReturnTypeInfo ReturnType;
  std::vector<FunctionParam> Parameters;
  std::optional<std::shared_ptr<ExportedClass>> ContainingClass;
};

constexpr std::array SpecialTypes = {std::string_view("std::span"),
                                     std::string_view("std::string_view")};

constexpr std::array SpecialReturnTypes = {
    std::string_view("std::span"), std::string_view("std::vector"),
    std::string_view("std::string"), std::string_view("std::string_view")};

void processFunctionDecl(
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    std::vector<FunctionInfo> &FunctionInfos,
    const clang::HushExportAttr *HushExportAttr, const clang::FunctionDecl *D);

#endif // FUNCTIONPARSER_H
