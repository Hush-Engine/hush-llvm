#include "ASTExtractor.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

namespace hush_reflection {
namespace {

// Reports an invalid hush annotation configuration.
void reportConfigError(clang::DiagnosticsEngine &Diags,
                       clang::SourceLocation Loc, llvm::StringRef Message) {
  unsigned Id = Diags.getDiagnosticIDs()->getCustomDiagID(
      clang::DiagnosticIDs::Error, "%0");
  Diags.Report(Loc, Id) << Message;
}

// Reads a string literal expression, returns nullopt when it is not one.
std::optional<std::string> getStringLiteral(const clang::Expr *E) {
  const auto *Lit = llvm::dyn_cast<clang::StringLiteral>(E->IgnoreImplicit());
  if (Lit == nullptr || !Lit->isOrdinary()) {
    return std::nullopt;
  }
  return Lit->getString().str();
}

// Reads an integer constant expression, returns nullopt when it is not one.
std::optional<int64_t> getIntConstant(const clang::Expr *E,
                                      clang::ASTContext &Ctx) {
  E = E->IgnoreParenImpCasts();
  clang::Expr::EvalResult Result;
  if (!E->isValueDependent() &&
      E->EvaluateAsInt(Result, Ctx, clang::Expr::SE_NoSideEffects)) {
    if (!Result.Val.getInt().isRepresentableByInt64())
      return std::nullopt;
    return Result.Val.getInt().getExtValue();
  }
  return std::nullopt;
}

// Reads the metadata pairs of a [[hush::meta]] attribute. Pairs are plain
// string literals: [[hush::meta("key", "value", ...)]].
std::vector<MetaPair> extractMetaPairs(const clang::HushMetaAttr *Meta,
                                       clang::DiagnosticsEngine &Diags) {
  std::vector<MetaPair> Pairs;
  if (Meta == nullptr) {
    return Pairs;
  }

  llvm::SmallVector<const clang::Expr *, 8> Args;
  for (const clang::Expr *E : Meta->metaArgs()) {
    Args.push_back(E);
  }
  size_t Count = Args.size();
  if (Count % 2 != 0) {
    reportConfigError(Diags, Meta->getLocation(),
                      "hush::meta expects an even number of string literal "
                      "arguments (key/value pairs)");
    return Pairs;
  }

  for (size_t I = 0; I < Count; I += 2) {
    std::optional<std::string> Key = getStringLiteral(Args[I]);
    std::optional<std::string> Value = getStringLiteral(Args[I + 1]);
    if (!Key.has_value() || !Value.has_value()) {
      reportConfigError(Diags, Meta->getLocation(),
                        "hush::meta arguments must be string literals");
      continue;
    }
    Pairs.push_back({*Key, *Value});
  }
  return Pairs;
}

// Builds the canonical name of a class: the qualified name with dots instead
// of double colons, so every language hashes the same name.
// An explicit Hush::Reflection::name("...") override wins when present.
std::string makeCanonicalName(const clang::CXXRecordDecl *Decl,
                              llvm::StringRef NameOverride) {
  if (!NameOverride.empty()) {
    return NameOverride.str();
  }

  std::string Name = Decl->getQualifiedNameAsString();
  std::string Canonical;
  Canonical.reserve(Name.size());
  for (size_t I = 0; I < Name.size(); ++I) {
    if (Name[I] == ':' && I + 1 < Name.size() && Name[I + 1] == ':') {
      Canonical += '.';
      ++I;
    } else {
      Canonical += Name[I];
    }
  }
  return Canonical;
}

// Checks whether the record derives (directly or indirectly) from the class
// with the given qualified name.
bool derivesFrom(const clang::CXXRecordDecl *Decl, llvm::StringRef BaseName) {
  if (Decl == nullptr || !Decl->hasDefinition()) {
    return false;
  }
  for (const clang::CXXBaseSpecifier &Base : Decl->bases()) {
    const clang::CXXRecordDecl *BaseDecl =
        Base.getType()->getAsCXXRecordDecl();
    if (BaseDecl == nullptr) {
      continue;
    }
    if (BaseDecl->getQualifiedNameAsString() == BaseName ||
        derivesFrom(BaseDecl, BaseName)) {
      return true;
    }
  }
  return false;
}

// Checks whether the record has a constructor whose first parameter is a
// Hush::Scene reference. Systems need it so the engine can create them.
bool hasSceneConstructor(const clang::CXXRecordDecl *Decl) {
  if (Decl->isAbstract()) {
    return false;
  }
  size_t Matches = 0;
  for (const clang::CXXConstructorDecl *Ctor : Decl->ctors()) {
    if (Ctor->isDeleted() || Ctor->getNumParams() == 0 ||
        Ctor->getMinRequiredArguments() > 1) {
      continue;
    }
    const clang::ParmVarDecl *First = Ctor->getParamDecl(0);
    const clang::QualType Type = First->getType();
    if (!Type->isLValueReferenceType()) {
      continue;
    }
    const clang::CXXRecordDecl *ParamRecord =
        Type->getPointeeCXXRecordDecl();
    if (ParamRecord != nullptr &&
        ParamRecord->getQualifiedNameAsString() == "Hush::Scene") {
      ++Matches;
    }
  }
  return Matches == 1;
}

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
                             const clang::HushPropertyAttr *Property,
                             clang::DiagnosticsEngine &Diags) {
  clang::PrintingPolicy PP = Field->getASTContext().getPrintingPolicy();

  FieldModel Model;
  Model.Name = Field->getName().str();
  Model.TypeName = Field->getType().getAsString(PP);
  Model.ParentClassName = Field->getParent()->getQualifiedNameAsString();
  Model.VisitorFieldName = Model.Name + "Visitor";
  Model.Metadata = extractMetaPairs(Field->getAttr<clang::HushMetaAttr>(),
                                    Diags);

  // Extract getter/setter from attribute config
  for (const auto *Expr : Property->propertyConfig()) {
    const auto *Call = llvm::dyn_cast<clang::CallExpr>(Expr);
    if (!Call || !Call->getDirectCallee())
      continue;

    std::string CalleeName =
        Call->getDirectCallee()->getQualifiedNameAsString();

    const clang::Expr *AccessorExpr =
        Call->getNumArgs() == 1 ? Call->getArg(0)->IgnoreParenImpCasts()
                                : nullptr;
    if (const auto *AddressOf =
            llvm::dyn_cast_or_null<clang::UnaryOperator>(AccessorExpr)) {
      if (AddressOf->getOpcode() == clang::UO_AddrOf)
        AccessorExpr = AddressOf->getSubExpr()->IgnoreParenImpCasts();
    }
    const auto *AccessorRef =
        llvm::dyn_cast_or_null<clang::DeclRefExpr>(AccessorExpr);
    const auto *Accessor =
        AccessorRef == nullptr
            ? nullptr
            : llvm::dyn_cast<clang::CXXMethodDecl>(AccessorRef->getDecl());

    if (CalleeName == "Hush::Reflection::Getter") {
      if (Call->getNumArgs() != 1 || Accessor == nullptr ||
          Accessor->isStatic() || Accessor->getNumParams() != 0 ||
          Accessor->getParent() != Field->getParent()) {
        reportConfigError(Diags, Call->getExprLoc(),
                          "Hush::Reflection::Getter expects a zero-argument "
                          "member function of the reflected class");
      } else {
        Model.HasCustomGetter = true;
        Model.GetterName = Accessor->getNameAsString();
      }
    } else if (CalleeName == "Hush::Reflection::Setter") {
      if (Call->getNumArgs() != 1 || Accessor == nullptr ||
          Accessor->isStatic() || Accessor->getNumParams() != 1 ||
          Accessor->getParent() != Field->getParent()) {
        reportConfigError(Diags, Call->getExprLoc(),
                          "Hush::Reflection::Setter expects a one-argument "
                          "member function of the reflected class");
      } else {
        Model.HasCustomSetter = true;
        Model.SetterName = Accessor->getNameAsString();
      }
    } else {
      reportConfigError(Diags, Call->getExprLoc(),
                        "unknown hush::property configuration '" +
                            CalleeName + "'");
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

FunctionModel extractFunctionModel(const clang::CXXMethodDecl *Method,
                                   clang::DiagnosticsEngine &Diags) {
  clang::PrintingPolicy PP = Method->getASTContext().getPrintingPolicy();

  FunctionModel Model;
  Model.Name = Method->getNameAsString();
  Model.ParentClassName = getParentClassName(Method);
  Model.ReturnsVoid = Method->getReturnType()->isVoidType();
  Model.Metadata = extractMetaPairs(Method->getAttr<clang::HushMetaAttr>(),
                                    Diags);

  for (const auto *Param : Method->parameters()) {
    Model.Params.push_back(extractParamModel(Param, PP));
  }

  return Model;
}

// Reads the configuration expressions of [[hush::reflect(...)]]. Only
// Hush::Reflection::name("...") is supported for now.
void extractReflectConfig(const clang::HushReflectAttr *Reflect,
                          clang::DiagnosticsEngine &Diags,
                          std::string &NameOverride) {
  if (Reflect == nullptr) {
    return;
  }
  for (const clang::Expr *Expr : Reflect->reflectConfig()) {
    const auto *Call =
        llvm::dyn_cast<clang::CallExpr>(Expr->IgnoreImplicit());
    if (Call == nullptr || Call->getDirectCallee() == nullptr) {
      reportConfigError(Diags, Expr->getExprLoc(),
                        "unsupported hush::reflect configuration, expected "
                        "Hush::Reflection::name(\"...\")");
      continue;
    }
    std::string Callee = Call->getDirectCallee()->getQualifiedNameAsString();
    if (Callee == "Hush::Reflection::name") {
      if (Call->getNumArgs() != 1) {
        reportConfigError(Diags, Call->getExprLoc(),
                          "Hush::Reflection::name expects one argument");
        continue;
      }
      std::optional<std::string> Name = getStringLiteral(Call->getArg(0));
      if (!Name.has_value()) {
        reportConfigError(Diags, Call->getExprLoc(),
                          "Hush::Reflection::name expects a string literal");
        continue;
      }
      NameOverride = *Name;
    } else {
      reportConfigError(Diags, Call->getExprLoc(),
                        "unknown hush::reflect configuration '" + Callee +
                            "'");
    }
  }
}

// Reads the configuration expressions of [[hush::system(...)]]. Only
// Hush::Reflection::order(n) is supported for now.
uint32_t extractSystemConfig(const clang::HushSystemAttr *System,
                             clang::ASTContext &Ctx,
                             clang::DiagnosticsEngine &Diags) {
  uint32_t Order = 0;
  if (System == nullptr) {
    return Order;
  }
  for (const clang::Expr *Expr : System->systemConfig()) {
    const auto *Call =
        llvm::dyn_cast<clang::CallExpr>(Expr->IgnoreImplicit());
    if (Call == nullptr || Call->getDirectCallee() == nullptr) {
      reportConfigError(Diags, Expr->getExprLoc(),
                        "unsupported hush::system configuration, expected "
                        "Hush::Reflection::order(n)");
      continue;
    }
    std::string Callee = Call->getDirectCallee()->getQualifiedNameAsString();
    if (Callee == "Hush::Reflection::order") {
      if (Call->getNumArgs() != 1) {
        reportConfigError(Diags, Call->getExprLoc(),
                          "Hush::Reflection::order expects one argument");
        continue;
      }
      std::optional<int64_t> Value = getIntConstant(Call->getArg(0), Ctx);
      if (!Value.has_value() || *Value < 0 || *Value > 255) {
        reportConfigError(
            Diags, Call->getExprLoc(),
            "Hush::Reflection::order expects an integer between 0 and 255");
        continue;
      }
      Order = static_cast<uint32_t>(*Value);
    } else {
      reportConfigError(Diags, Call->getExprLoc(),
                        "unknown hush::system configuration '" + Callee + "'");
    }
  }
  return Order;
}

} // anonymous namespace

ClassModel extractClassModel(const clang::CXXRecordDecl *Decl,
                             clang::DiagnosticsEngine &Diags) {
  ClassModel Model;
  Model.QualifiedName = Decl->getQualifiedNameAsString();
  Model.UnqualifiedName = Decl->getName().str();

  // Templates and anonymous namespaces cannot produce a stable canonical
  // name, so they cannot be reflected.
  if (Decl->getDescribedClassTemplate() != nullptr ||
      llvm::isa<clang::ClassTemplateSpecializationDecl>(Decl)) {
    reportConfigError(Diags, Decl->getLocation(),
                      "template classes cannot be reflected");
    return Model;
  }
  if (Decl->isInAnonymousNamespace()) {
    reportConfigError(Diags, Decl->getLocation(),
                      "classes in anonymous namespaces cannot be reflected");
    return Model;
  }

  std::string NameOverride;
  extractReflectConfig(Decl->getAttr<clang::HushReflectAttr>(), Diags,
                       NameOverride);
  Model.CanonicalName = makeCanonicalName(Decl, NameOverride);

  Model.IsBuiltin = Decl->hasAttr<clang::HushBuiltinAttr>();
  Model.IsComponent = Decl->hasAttr<clang::HushComponentAttr>();
  Model.Metadata =
      extractMetaPairs(Decl->getAttr<clang::HushMetaAttr>(), Diags);
  for (const MetaPair &Meta : Model.Metadata) {
    if (Meta.Key == "hush.builtin" || Meta.Key == "hush.component" ||
        Meta.Key == "hush.system") {
      reportConfigError(Diags, Decl->getLocation(),
                        "hush metadata keys beginning with 'hush.' are "
                        "reserved for generated type markers");
    }
  }

  const auto *System = Decl->getAttr<clang::HushSystemAttr>();
  Model.IsSystem = System != nullptr;
  if (Model.IsSystem) {
    Model.SystemOrder = extractSystemConfig(System, Decl->getASTContext(), Diags);

    if (!derivesFrom(Decl, "Hush::ISystem")) {
      reportConfigError(Diags, Decl->getLocation(),
                        "hush::system classes must derive from Hush::ISystem");
    }
    if (!hasSceneConstructor(Decl)) {
      reportConfigError(
          Diags, Decl->getLocation(),
          "hush::system classes need a constructor taking Hush::Scene &");
    }
  }

  // Extract fields with HushProperty attribute
  for (const auto *Field : Decl->fields()) {
    if (const auto *Property = Field->getAttr<clang::HushPropertyAttr>()) {
      Model.Fields.push_back(extractFieldModel(Field, Property, Diags));
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
      Model.Functions.push_back(extractFunctionModel(Method, Diags));
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

std::optional<ModuleInitFunction>
extractModuleInitFunction(const clang::FunctionDecl *Decl,
                          clang::DiagnosticsEngine &Diags) {
  if (Decl->getNumParams() != 0 || !Decl->getReturnType()->isVoidType()) {
    reportConfigError(Diags, Decl->getLocation(),
                      "hush::module_init functions must be void and take no "
                      "arguments");
    return std::nullopt;
  }
  if (Decl->isDeleted() || Decl->getTemplatedKind() !=
                               clang::FunctionDecl::TK_NonTemplate ||
      Decl->isInAnonymousNamespace()) {
    reportConfigError(Diags, Decl->getLocation(),
                      "hush::module_init requires a non-template, non-deleted "
                      "function outside anonymous namespaces");
    return std::nullopt;
  }

  ModuleInitFunction Model;
  Model.QualifiedName = Decl->getQualifiedNameAsString();
  return Model;
}

} // namespace hush_reflection
