#include "ASTExtractor.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "llvm/Support/raw_ostream.h"

namespace hush_reflection {
namespace {

ParamModel extractParamModel(const clang::ParmVarDecl *Param,
                             const clang::PrintingPolicy &PP) {
  ParamModel Model;
  Model.TypeName = Param->getType().getAsString(PP);
  Model.CanonicalTypeName = Param->getType()
                                .getNonReferenceType()
                                .getCanonicalType()
                                .getAsString(PP);
  Model.IsPointer = Param->getType()->isPointerType();
  return Model;
}

FieldModel extractFieldModel(const clang::FieldDecl *Field,
                             const clang::HushPropertyAttr *Property) {
  clang::PrintingPolicy PP = Field->getASTContext().getPrintingPolicy();

  FieldModel Model;
  Model.Name = Field->getName().str();
  Model.TypeName = Field->getType().getAsString(PP);
  Model.ParentClassName = Field->getParent()->getQualifiedNameAsString();
  Model.VisitorFieldName = Model.Name + "Visitor";

  // Extract getter/setter from attribute config
  for (const auto *Expr : Property->propertyConfig()) {
    const auto *Call = llvm::dyn_cast<clang::CallExpr>(Expr);
    if (!Call || !Call->getDirectCallee())
      continue;

    std::string CalleeName = Call->getDirectCallee()->getNameAsString();

    if (CalleeName == "Hush::Reflection::Getter") {
      Model.HasCustomGetter = true;
      if (Call->getNumArgs() > 0) {
        Model.GetterName = Call->getArg(0)->getSourceRange().printToString(
            Field->getASTContext().getSourceManager());
      }
    } else if (CalleeName == "Hush::Reflection::Setter") {
      Model.HasCustomSetter = true;
      if (Call->getNumArgs() > 0) {
        Model.SetterName = Call->getArg(0)->getSourceRange().printToString(
            Field->getASTContext().getSourceManager());
      }
    }
  }

  return Model;
}

std::string
getParentClassName(const clang::FunctionDecl *FD) {
  const auto *Parent =
      llvm::dyn_cast<clang::CXXRecordDecl>(FD->getParent());
  if (!Parent) {
    llvm::errs() << "Parent decl not found for function: "
                 << FD->getNameAsString() << "\n";
    return "";
  }
  return Parent->getQualifiedNameAsString();
}

ConstructorModel
extractConstructorModel(const clang::CXXConstructorDecl *Ctor) {
  clang::PrintingPolicy PP = Ctor->getASTContext().getPrintingPolicy();

  ConstructorModel Model;
  Model.ParentClassName = getParentClassName(Ctor);

  for (const auto *Param : Ctor->parameters()) {
    Model.Params.push_back(extractParamModel(Param, PP));
  }

  return Model;
}

FunctionModel extractFunctionModel(const clang::CXXMethodDecl *Method) {
  clang::PrintingPolicy PP = Method->getASTContext().getPrintingPolicy();

  FunctionModel Model;
  Model.Name = Method->getNameAsString();
  Model.ParentClassName = getParentClassName(Method);
  Model.ReturnsVoid = Method->getReturnType()->isVoidType();

  for (const auto *Param : Method->parameters()) {
    Model.Params.push_back(extractParamModel(Param, PP));
  }

  return Model;
}

} // anonymous namespace

ClassModel extractClassModel(const clang::CXXRecordDecl *Decl) {
  ClassModel Model;
  Model.QualifiedName = Decl->getQualifiedNameAsString();
  Model.UnqualifiedName = Decl->getName().str();

  // Extract fields with HushProperty attribute
  for (const auto *Field : Decl->fields()) {
    if (const auto *Property = Field->getAttr<clang::HushPropertyAttr>()) {
      Model.Fields.push_back(extractFieldModel(Field, Property));
    }
  }

  // Extract methods with HushFunction attribute (skip static, ctors, dtors)
  for (const auto *Method : Decl->methods()) {
    if (Method->isStatic())
      continue;
    if (llvm::isa<clang::CXXConstructorDecl>(Method) ||
        llvm::isa<clang::CXXDestructorDecl>(Method))
      continue;

    if (Method->getAttr<clang::HushFunctionAttr>()) {
      Model.Functions.push_back(extractFunctionModel(Method));
    }
  }

  // Extract constructors with HushFunction attribute
  for (const auto *Ctor : Decl->ctors()) {
    if (Ctor->getAttr<clang::HushFunctionAttr>()) {
      Model.Constructors.push_back(extractConstructorModel(Ctor));
    }
  }

  return Model;
}

} // namespace hush_reflection
