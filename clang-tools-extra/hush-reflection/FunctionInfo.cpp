#include "FunctionInfo.h"

FunctionInfo::FunctionInfo(const clang::FunctionDecl *FunctionDecl,
                           const clang::HushFunctionAttr *HushFunctionAttr)
    : FunctionDecl(FunctionDecl), HushFunctionAttr(HushFunctionAttr) {
  FunctionName = FunctionDecl->getNameAsString();

  const auto ParentDecl =
      llvm::dyn_cast<clang::CXXRecordDecl>(FunctionDecl->getParent());

  if (ParentDecl == nullptr) {
    llvm::errs() << "Parent decl not found";
    llvm::errs() << "Function: " << FunctionDecl->getNameAsString() << "\n";
  }

  ParentClassName = ParentDecl ? ParentDecl->getQualifiedNameAsString() : "";
}

std::string FunctionInfo::generateReflectionCode() const {

  const auto PrintingPolicy = FunctionDecl->getASTContext().getPrintingPolicy();
  std::string Code;
  Code.reserve(8192);

  Code += ".AddFunction(Hush::Reflection::FunctionInfo::Create<";
  Code += ParentClassName;
  if (FunctionDecl->parameters().size() > 0) {
    Code += ", ";
  }

  int ParamIndex = 0;
  for (const auto &Param : FunctionDecl->parameters()) {
    Code += Param->getType().getAsString(PrintingPolicy);

    if (ParamIndex < FunctionDecl->getNumParams() - 1) {
      Code += ", ";
    }
    ++ParamIndex;
  }

  Code += ">(\\\n"
          "[](std::span<const Hush::Reflection::VariantView> args) -> "
          "Hush::Result<Hush::Reflection::Variant, "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError> {\\\n"
          "if (args.size() != ";
  Code += std::to_string(FunctionDecl->getNumParams() + 1); // +1 for the instance
  Code += ") {\\\n"
          "return Hush::Reflection::FunctionInfo::EFunctionInfoError::"
          "InvalidArgsCount;\\\n"
          "}\\\n";

  Code += "auto instanceResult = args[0].Get<";
  Code += ParentClassName;
  Code += ">();\\\n"
          "if (instanceResult.has_error()) {\\\n"
          "return "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError::InvalidType;\\\n"
          "}\\\n"
          "auto *instance = instanceResult.value();\\\n";

  ParamIndex = 1;
  for (const auto &Param : FunctionDecl->parameters()) {
    Code += "auto param";
    Code += std::to_string(ParamIndex - 1);
    Code += "Result = args[";
    Code += std::to_string(ParamIndex);
    Code += "].Get<";
    // Get the type WITHOUT pointers or references
    Code += Param->getType().getNonReferenceType().getCanonicalType().getAsString(PrintingPolicy);
    Code += ">();\\\n"
            "if (param";
    Code += std::to_string(ParamIndex - 1);
    Code += "Result.has_error()) {\\\n"
            "return Hush::Reflection::FunctionInfo::EFunctionInfoError::InvalidType;\\\n"
            "}\\\n";
  }
  if (FunctionDecl->getReturnType()->isVoidType()) {
    Code += "instance->";
  } else {
    Code += "auto result = ";
  }
  Code += FunctionName;
  Code += "(";
  for (int I = 1; I <= FunctionDecl->getNumParams(); ++I) {
    const clang::ParmVarDecl *param = FunctionDecl->getParamDecl(I - 1);
    // Is the param a pointer?
    if (param->getType()->isPointerType()) {
      Code += "param" + std::to_string(I - 1) + "Result.value()";
    } else {
      Code += "*param" + std::to_string(I - 1) + "Result.value()";
    }

    if (I < FunctionDecl->getNumParams()) {
      Code += ", ";
    }
  }

  // Is the function returning a value?
  if (FunctionDecl->getReturnType()->isVoidType()) {
    Code += ");\\\n"
            "return Hush::Reflection::Variant();\\\n";
  } else {
    Code += ");\\\n"
            "return Hush::Reflection::Variant(result);\\\n";
  }
  Code += "}, \"";
  Code += FunctionName;
  Code += "\"))\\\n";

  return Code;
}