#pragma once
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.

#include <clang/ASTMatchers/ASTMatchers.h>

class ClassField {
  struct FieldGetter {
    std::string Name;
    bool IsFunction;
  };
  struct FieldSetter {
    std::string Name;
    bool IsFunction;
  };

public:
  ClassField(const clang::FieldDecl *fieldDecl,
             const clang::HushPropertyAttr *propertyAttr);

  std::string generatePropertyReflectionCode() const;

  std::string generateSerializeCode() const;

  std::string getVisitorFieldType() const;

  llvm::StringRef getVisitorFieldName() const;

  llvm::StringRef getFieldName() const;

private:

  void calculateGetterAndSetter();

  std::string generateSetterCode() const;

  std::string generateGetterCode() const;

  const clang::FieldDecl *FieldDecl;
  const clang::HushPropertyAttr *PropertyAttr;

  FieldGetter FieldGetter;
  FieldSetter FieldSetter;
  std::string FieldName;
  std::string FieldTypeName;
  std::string ParentClassName;
  std::string VisitorFieldName;
};
