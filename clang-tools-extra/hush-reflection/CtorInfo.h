#pragma once
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.


#include <clang/ASTMatchers/ASTMatchers.h>
class CtorInfo {
  const clang::FunctionDecl *FunctionDecl;
  const clang::HushFunctionAttr *HushFunctionAttr;
  std::string FunctionName;
  std::string ParentClassName;

public:
  CtorInfo(const clang::FunctionDecl *FunctionDecl,
               const clang::HushFunctionAttr *HushFunctionAttr);

  std::string generateReflectionCode() const;
};