#pragma once
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.

#include <clang/ASTMatchers/ASTMatchers.h>

class FunctionInfo {
  const clang::FunctionDecl *FunctionDecl;
  const clang::HushFunctionAttr *HushFunctionAttr;
  std::string FunctionName;
  std::string ParentClassName;

public:
  FunctionInfo(const clang::FunctionDecl *FunctionDecl,
               const clang::HushFunctionAttr *HushFunctionAttr);

  std::string generateReflectionCode() const;
};
