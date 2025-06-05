#include "CtorInfo.h"

CtorInfo::CtorInfo(const clang::FunctionDecl *FunctionDecl,
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

std::string CtorInfo::generateReflectionCode() const {
  const auto PrintingPolicy = FunctionDecl->getASTContext().getPrintingPolicy();

  std::string Code;
  Code.reserve(8192);

  Code += "   .AddConstructor(Hush::Reflection::FunctionInfo::Create<";
  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    if (I > 0 && I < FunctionDecl->getNumParams() - 1) {
      Code += ", ";
    }
    Code += FunctionDecl->getParamDecl(I)
                ->getType()
                .getNonReferenceType()
                .getCanonicalType()
                .getAsString(PrintingPolicy);
  }
  Code += ">(\\\n";
  Code += "      [](std::span<const Hush::Reflection::VariantView> args) -> "
          "Hush::Result<Hush::Reflection::Variant, "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError> {\\\n";
  Code += "        if (args.size() != ";
  Code += std::to_string(FunctionDecl->getNumParams());
  Code += ") {\\\n"
          "          return "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError::"
          "InvalidArgsCount;\\\n";
  Code += "        }\\\n";

  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    const clang::ParmVarDecl *Param = FunctionDecl->getParamDecl(I);
    Code += "        auto param";
    Code += std::to_string(I);
    Code += "Result = args[";
    Code += std::to_string(I);
    Code += "].Get<";
    // Get the type of the parameter
    Code +=
        Param->getType().getNonReferenceType().getCanonicalType().getAsString(
            PrintingPolicy);
    Code += ">();\\\n";
    Code += "        if (!param";
    Code += std::to_string(I);
    Code +=
        "Result.has_value()) {\\\n"
        "          return "
        "Hush::Reflection::FunctionInfo::EFunctionInfoError::InvalidType;\\\n"
        "}\\\n";
  }

  // Now, we need to generate the call to the constructor
  Code += "        return Hush::Reflection::Variant::CreateInPlace<";
  Code += ParentClassName;
  Code += ">(";

  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    const clang::ParmVarDecl *Param = FunctionDecl->getParamDecl(I);

    // Is the parameter a pointer?
    if (Param->getType()->isPointerType()) {
      Code += "param";
      Code += std::to_string(I);
      Code += "Result.value()";
    } else {
      Code += "*param";
      Code += std::to_string(I);
      Code += "Result.value()";
    }
    if (I < FunctionDecl->getNumParams() - 1) {
      Code += ", ";
    }
  }
  Code += ");\\\n"
          "      }, \"";
  Code += ParentClassName;
  Code +=
      "\""
      "    ))\\\n"
      "   "
      ".AddInPlaceConstructor(Hush::Reflection::TypeInfo::InPlaceCtor::Create<";

  // Now, we need to generate the AddInPlaceCtor
  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    if (I > 0 && I < FunctionDecl->getNumParams() - 1) {
      Code += ", ";
    }
    Code += FunctionDecl->getParamDecl(I)
                ->getType()
                .getNonReferenceType()
                .getCanonicalType()
                .getAsString(PrintingPolicy);
  }
  Code += ">(\\\n";
  Code += "      [](void *mem, std::span<const Hush::Reflection::VariantView> "
          "args) -> Hush::Reflection::TypeInfo::EInPlaceConstructorError {\\\n";

  Code += "        if (args.size() != ";
  Code += std::to_string(FunctionDecl->getNumParams());
  Code += ") {\\\n"
          "          return "
          "Hush::Reflection::TypeInfo::EInPlaceConstructorError::"
          "NonMatchingArgs;\\\n"
          "        }\\\n";

  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    const clang::ParmVarDecl *Param = FunctionDecl->getParamDecl(I);
    Code += "        auto param";
    Code += std::to_string(I);
    Code += "Result = args[";
    Code += std::to_string(I);
    Code += "].Get<";
    // Get the type of the parameter
    Code +=
        Param->getType().getNonReferenceType().getCanonicalType().getAsString(
            PrintingPolicy);
    Code += ">();\\\n";
    Code += "        if (!param";
    Code += std::to_string(I);
    Code +=
        "Result.has_value()) {\\\n"
        "          return "
        "Hush::Reflection::TypeInfo::EInPlaceConstructorError::InvalidType;\\\n"
        "}\\\n";
  }
  // Now, we need to generate the call to the constructor
  Code += "  std::construct_at(static_cast<";
  Code += ParentClassName;
  Code += " *>(mem)";
  if (FunctionDecl->getNumParams() > 0) {
    Code += ", ";
  }
  for (size_t I = 0; I < FunctionDecl->getNumParams(); ++I) {
    auto *Param = FunctionDecl->getParamDecl(I);
    // Is the parameter a pointer?
    if (!Param->getType()->isPointerType()) {
      Code += "*param";
      Code += std::to_string(I);
      Code += "Result.value()";
    } else {
      Code += "param";
      Code += std::to_string(I);
      Code += "Result.value()";
    }
  }
  Code += ");\\\n"
          "        return "
          "Hush::Reflection::TypeInfo::EInPlaceConstructorError::None;\\\n"
          "      }\\\n"
          "    )\\\n";
  Code += "  )\\\n";

  return Code;
}