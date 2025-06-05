#pragma once
#include "ClassField.h"
#include "CtorInfo.h"
#include "FunctionInfo.h"

#include "clang/Tooling/Tooling.h"

class ReflectedClass {
public:
  explicit ReflectedClass(const clang::CXXRecordDecl *decl);

  bool generateReflectionCode(llvm::raw_ostream &os) const;

  bool generateSerializeCode(llvm::raw_ostream &os) const;

  bool generateDeserializeCode(llvm::raw_ostream &os) const;

private:
  std::vector<ClassField> getFields() const;

  void getDeclaredFunctions();

  void getConstructors();

  std::vector<ClassField> Fields;
  std::vector<FunctionInfo> Functions;
  std::vector<CtorInfo> Constructors;

  const clang::CXXRecordDecl *m_decl;

  std::string m_className;
};