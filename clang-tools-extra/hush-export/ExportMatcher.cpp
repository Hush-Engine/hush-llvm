//===-- ExportMatcher.cpp - AST matcher that produces CBindingIR directly -===//

#include "ExportMatcher.h"
#include "ParserCommon.h"

#include "clang/AST/Type.h"

using namespace hush;

// ===== Helpers =====

/// Strip C++ tag keywords (class/struct/enum) that Clang's canonical printer
/// adds to type names.
static std::string stripTagKeywords(std::string Name) {
  for (llvm::StringRef Prefix : {"class ", "struct ", "enum "}) {
    if (llvm::StringRef(Name).starts_with(Prefix))
      Name.erase(0, Prefix.size());
  }
  return Name;
}

std::string ExportMatcher::makeDefaultExportName(const std::string &QN) {
  std::string Name = QN;
  std::replace(Name.begin(), Name.end(), ':', '_');
  return Name;
}

std::string ExportMatcher::getUnqualifiedName(const std::string &QN) {
  size_t Pos = QN.find_last_of(':');
  return Pos != std::string::npos ? QN.substr(Pos + 1) : QN;
}

std::string ExportMatcher::normalizeStdIntType(const std::string &Type) {
  std::string Result = Type;
  auto Pos = Result.find("std::");
  while (Pos != std::string::npos) {
    // Only strip std:: if followed by uint or int
    if (Result.size() > Pos + 5 &&
        (Result.substr(Pos + 5, 4) == "uint" ||
         Result.substr(Pos + 5, 3) == "int")) {
      Result.erase(Pos, 5);
    } else {
      break;
    }
    Pos = Result.find("std::", Pos);
  }
  return Result;
}

bool ExportMatcher::isContainerReturn(const TypeResolution &Res) {
  return Res.isContainer;
}

// ===== Function-pointer + typedef alias helpers =====

CType ExportMatcher::buildFuncPointerCType(clang::QualType FpType,
                                            llvm::StringRef ParamName,
                                            clang::ASTContext &Ctx) {
  // Walk through typedef sugar at the outer pointer level so we land on
  // the actual function pointer. We must NOT use getCanonicalType() here:
  // that strips typedef sugar from the inner parameter types too, which
  // is exactly what we need to preserve so the registry can map them.
  clang::QualType OuterType = FpType;
  while (const auto *TDT = OuterType->getAs<clang::TypedefType>()) {
    OuterType = TDT->desugar();
  }
  clang::QualType Pointee = OuterType->getPointeeType();
  const auto *FPT = Pointee->getAs<clang::FunctionProtoType>();
  if (!FPT) {
    // Fallback: emit canonical string with the name inserted into "(*)".
    std::string TypeStr = stripTagKeywords(
        FpType.getCanonicalType().getAsString(Ctx.getPrintingPolicy()));
    if (!ParamName.empty()) {
      size_t CloseParen = TypeStr.find(')');
      if (CloseParen != std::string::npos)
        TypeStr.replace(CloseParen, 1, ParamName.str() + ")");
    }
    return CType::makeFuncPointer(TypeStr);
  }

  // Resolve a single sub-type (return or parameter) through the registry,
  // falling back to the canonical name when no translator claims it.
  // Trigger lazy alias discovery first so class-scope typedefs that haven't
  // been visited yet (e.g., a callback referencing Entity::EntityId before
  // the Entity class is matched) get registered before we resolve.
  auto resolveSubType = [&](clang::QualType T) -> std::string {
    if (T->isVoidType())
      return "void";
    ensureTypedefAliasRegistered(T, Ctx);
    if (auto Res = Registry_.resolve(T))
      return Res->cType.toString();
    std::string S = stripTagKeywords(
        T.getCanonicalType().getAsString(Ctx.getPrintingPolicy()));
    std::replace(S.begin(), S.end(), ':', '_');
    return S;
  };

  std::string RetStr = resolveSubType(FPT->getReturnType());

  std::string ParamsStr;
  bool First = true;
  for (clang::QualType ArgType : FPT->param_types()) {
    if (!First)
      ParamsStr += ", ";
    First = false;
    ParamsStr += resolveSubType(ArgType);
  }
  if (First)
    ParamsStr = "void";

  std::string Decl =
      RetStr + " (*" + ParamName.str() + ")(" + ParamsStr + ")";
  return CType::makeFuncPointer(Decl);
}

void ExportMatcher::addTypeAlias(const clang::TypedefNameDecl *TAD,
                                  const std::string &CName,
                                  clang::ASTContext &Ctx) {
  std::string CppName = TAD->getQualifiedNameAsString();
  if (Registry_.isRegistered(CppName))
    return;

  clang::QualType Underlying = TAD->getUnderlyingType();
  CTypeAlias Alias;
  Alias.name = CName;

  if (Underlying->isFunctionPointerType()) {
    CType FP = buildFuncPointerCType(Underlying, CName, Ctx);
    Alias.declaration = FP.funcPointerDecl;
  } else {
    auto Res = Registry_.resolve(Underlying);
    if (!Res || Res->isContainer)
      return;
    Alias.declaration = Res->cType.toString() + " " + CName;
  }

  IR_.typeAliases.push_back(Alias);
  Registry_.registerType(CppName, CType::makeBuiltin(CName));
}

void ExportMatcher::ensureTypedefAliasRegistered(clang::QualType Type,
                                                  clang::ASTContext &Ctx) {
  // Walk the typedef chain. Two paths:
  //  - Class-scope typedef (e.g., Entity::EntityId): trigger processClass
  //    on the containing record so the typedef's owning class registers
  //    it alongside its other typedefs.
  //  - Namespace-scope typedef (e.g., ObserverCallback_t): emit it
  //    directly via addTypeAlias.
  // Function-pointer typedefs whose inner parameter types reference
  // class-scope typedefs need both passes — that's why we walk the chain.
  clang::QualType Cur = Type;
  while (const auto *TDT = Cur->getAs<clang::TypedefType>()) {
    const clang::TypedefNameDecl *TAD = TDT->getDecl();
    if (Registry_.isRegistered(TAD->getQualifiedNameAsString()))
      return;

    const clang::DeclContext *DC = TAD->getDeclContext();
    if (DC->isRecord()) {
      if (const auto *RD = llvm::dyn_cast<clang::RecordDecl>(DC))
        ensureRegistered(RD);
      if (Registry_.isRegistered(TAD->getQualifiedNameAsString()))
        return;
    } else if ((DC->isNamespace() || DC->isTranslationUnit()) &&
               !Ctx.getSourceManager().isInSystemHeader(TAD->getLocation())) {
      std::string CName = makeDefaultExportName(TAD->getQualifiedNameAsString());
      addTypeAlias(TAD, CName, Ctx);
      return;
    }

    clang::QualType Desugared = TDT->desugar();
    if (Desugared == Cur)
      break;
    Cur = Desugared;
  }
}

// ===== Attribute parsing =====

ExportAttrOptions ExportMatcher::parseExportAttr(
    const clang::HushExportAttr *Attr, clang::ASTContext &Ctx) {
  ExportAttrOptions Opts;
  if (!Attr)
    return Opts;

  for (clang::Expr **It = Attr->exportConfig_begin();
       It != Attr->exportConfig_end(); ++It) {
    if (auto *Ref = llvm::dyn_cast<clang::DeclRefExpr>(
            (*It)->IgnoreImplicit())) {
      if (isHushExportIgnore(Ref))
        Opts.ignore = true;
      if (isHushExportHandle(Ref))
        Opts.asHandle = true;
    }
    if (auto *Call = llvm::dyn_cast_or_null<clang::CallExpr>(
            (*It)->IgnoreImplicit())) {
      if (Call->getDirectCallee()) {
        if (auto Name = getHushExportName(Call, Ctx))
          Opts.customName = *Name;
      }
    }
  }
  return Opts;
}

// ===== Constructor + glm registration =====

static bool hasDestructor(const clang::QualType QT) {
  return QT.isDestructedType() != clang::QualType::DK_none;
}

static bool isPOD(const clang::RecordDecl *D) {
  const auto &Context = D->getASTContext();
  clang::QualType RecordType = Context.getCanonicalTagType(D);
  return RecordType.isPODType(Context);
}

ExportMatcher::ExportMatcher() {
  // Set up the type registry with standard translators
  Registry_.addTranslator(std::make_unique<TypeAliasTranslator>());
  Registry_.addTranslator(std::make_unique<ContainerTranslator>());
  Registry_.addTranslator(std::make_unique<ZStringViewTranslator>());
  Registry_.addTranslator(std::make_unique<ResultTranslator>());
  Registry_.addTranslator(std::make_unique<BuiltinTranslator>());
  Registry_.addTranslator(std::make_unique<EnumTranslator>());
  Registry_.addTranslator(std::make_unique<ReferenceTranslator>());
  Registry_.addTranslator(std::make_unique<PointerTranslator>());
  Registry_.addTranslator(std::make_unique<RecordTranslator>());

  registerGlmTypes();
}

void ExportMatcher::registerGlmTypes() {
  constexpr std::array VecFields = {"x", "y", "z", "w"};

  struct VecDef {
    const char *prefix;
    const char *exportPrefix;
    const char *fieldType;
    int dims;
  };

  auto addVec = [&](const std::string &cppName, const std::string &exportName,
                     const char *fieldType, int dims) {
    CStruct s;
    s.name = exportName;
    s.cppName = cppName;
    for (int i = 0; i < dims; ++i) {
      CField f;
      f.name = VecFields[i];
      f.type = CType::makeBuiltin(fieldType);
      s.fields.push_back(f);
    }
    IR_.structs.push_back(s);
    Registry_.registerType(cppName, CType::makeStruct(exportName));
  };

  // Float vectors: vec2, vec3, vec4
  for (int D = 2; D <= 4; ++D) {
    std::string Dim = std::to_string(D);
    addVec("glm::vec" + Dim, "Vector" + Dim, "float", D);
  }

  // Double vectors: dvec2, dvec3, dvec4
  for (int D = 2; D <= 4; ++D) {
    std::string Dim = std::to_string(D);
    addVec("glm::dvec" + Dim, "DVector" + Dim, "double", D);
  }

  // Sized integer vectors
  const char *Sizes[] = {"8", "16", "32", "64"};
  for (const char *Sz : Sizes) {
    for (int D = 2; D <= 4; ++D) {
      std::string Dim = std::to_string(D);
      addVec("glm::u" + std::string(Sz) + "vec" + Dim,
             "U" + std::string(Sz) + "Vector" + Dim,
             ("uint" + std::string(Sz) + "_t").c_str(), D);
      addVec("glm::i" + std::string(Sz) + "vec" + Dim,
             "I" + std::string(Sz) + "Vector" + Dim,
             ("int" + std::string(Sz) + "_t").c_str(), D);
    }
  }

  // Float matrices: mat2, mat3, mat4
  for (int D = 2; D <= 4; ++D) {
    std::string Dim = std::to_string(D);
    CStruct s;
    s.name = "Matrix" + Dim;
    s.cppName = "glm::mat" + Dim;
    CField f;
    f.name = "m[" + Dim + "]";
    f.type = CType::makeBuiltin("float");
    s.fields.push_back(f);
    IR_.structs.push_back(s);
    Registry_.registerType(s.cppName, CType::makeStruct(s.name));
  }

  // Double matrices: dmat2, dmat3, dmat4
  for (int D = 2; D <= 4; ++D) {
    std::string Dim = std::to_string(D);
    CStruct s;
    s.name = "DMatrix" + Dim;
    s.cppName = "glm::dmat" + Dim;
    CField f;
    f.name = "m[" + Dim + "]";
    f.type = CType::makeBuiltin("double");
    s.fields.push_back(f);
    IR_.structs.push_back(s);
    Registry_.registerType(s.cppName, CType::makeStruct(s.name));
  }

  // Quaternion
  {
    CStruct s;
    s.name = "Quat";
    s.cppName = "glm::quat";
    for (int i = 0; i < 4; ++i) {
      CField f;
      f.name = VecFields[i];
      f.type = CType::makeBuiltin("float");
      s.fields.push_back(f);
    }
    IR_.structs.push_back(s);
    Registry_.registerType(s.cppName, CType::makeStruct(s.name));
  }
}

// ===== run() dispatch =====

void ExportMatcher::run(
    const clang::ast_matchers::MatchFinder::MatchResult &Result) {
  if (const auto *D =
          Result.Nodes.getNodeAs<clang::RecordDecl>("hushExportable")) {
    auto *Attr = D->getAttr<clang::HushExportAttr>();
    processClass(Attr, D);
  } else if (const auto *FD = Result.Nodes.getNodeAs<clang::FunctionDecl>(
                 "hushExportable")) {
    auto *Attr = FD->getAttr<clang::HushExportAttr>();
    processFunction(Attr, FD);
  } else if (const auto *ED =
                 Result.Nodes.getNodeAs<clang::EnumDecl>("hushExportable")) {
    auto *Attr = ED->getAttr<clang::HushExportAttr>();
    processEnum(Attr, ED);
  }
}

// ===== Enum processing =====

void ExportMatcher::processEnum(const clang::HushExportAttr *Attr,
                                const clang::EnumDecl *D) {
  if (!Attr)
    return;

  std::string CppName = D->getQualifiedNameAsString();
  if (ProcessedDecls_.count(CppName))
    return;

  auto Opts = parseExportAttr(Attr, D->getASTContext());
  if (Opts.ignore)
    return;

  std::string ExportName =
      Opts.customName.empty() ? makeDefaultExportName(CppName) : Opts.customName;

  CEnumDef EnumDef;
  EnumDef.name = ExportName;

  if (D->getIntegerTypeSourceInfo() == nullptr) {
    EnumDef.isPlainEnum = true;
  } else {
    EnumDef.isPlainEnum = false;
    EnumDef.underlyingType =
        normalizeStdIntType(D->getIntegerType().getAsString());
  }

  for (const auto *EC : D->enumerators())
    EnumDef.values.push_back({EC->getName().str(), EC->getInitVal().getExtValue()});

  IR_.enums.push_back(EnumDef);
  Registry_.registerType(CppName, CType::makeEnum(ExportName), /*isEnum=*/true);
  ProcessedDecls_.insert(CppName);
}

// ===== Class processing =====

std::optional<std::string>
ExportMatcher::ensureRegistered(const clang::RecordDecl *RD) {
  if (!RD)
    return std::nullopt;

  std::string CppName = RD->getQualifiedNameAsString();

  // Already registered?
  if (auto Res = Registry_.lookup(CppName))
    return Res->cType.name;

  // Has [[hush_export]]?
  auto *Attr = RD->getAttr<clang::HushExportAttr>();
  if (!Attr)
    return std::nullopt;

  processClass(Attr, RD);
  if (auto Res = Registry_.lookup(CppName))
    return Res->cType.name;

  return std::nullopt;
}

bool ExportMatcher::ensureEnumExported(clang::QualType EnumType,
                                       clang::SourceLocation Loc) {
  const auto *ET = EnumType->getAs<clang::EnumType>();
  if (!ET)
    return true; // Not an enum, nothing to check.

  const clang::EnumDecl *ED = ET->getDecl();
  std::string CppName = ED->getQualifiedNameAsString();

  // Already processed and emitted.
  if (Registry_.isRegistered(CppName))
    return true;

  // Annotated but not visited by the matcher yet. This happens when a
  // nested enum is traversed after the function that uses it. Process it
  // now. processEnum dedups via ProcessedDecls_.
  if (auto *Attr = ED->getAttr<clang::HushExportAttr>()) {
    processEnum(Attr, ED);
    if (Registry_.isRegistered(CppName))
      return true;
    // Otherwise it was [[hush::export(ignore)]]. Treat as not exported.
  }

  unsigned DiagID = ED->getASTContext().getDiagnostics().getCustomDiagID(
      clang::DiagnosticsEngine::Error,
      "enum %0 is not exported, cannot be used as a public-interface type");
  ED->getASTContext().getDiagnostics().Report(Loc, DiagID) << CppName;
  return false;
}

// Field options parsed from [[hush_export(...)]] on a field.
struct FieldAttrOptions {
  bool ignore = false;
  std::string customName;
};

static FieldAttrOptions parseFieldAttr(const clang::FieldDecl *Field) {
  FieldAttrOptions Opts;
  auto *Attr = Field->getAttr<clang::HushExportAttr>();
  if (!Attr)
    return Opts;

  for (auto *ArgExpr : Attr->exportConfig()) {
    if (auto *Ref =
            llvm::dyn_cast<clang::DeclRefExpr>(ArgExpr->IgnoreImplicit())) {
      if (isHushExportIgnore(Ref))
        Opts.ignore = true;
    }
    if (auto *Call =
            llvm::dyn_cast<clang::CallExpr>(ArgExpr->IgnoreImplicit())) {
      if (auto Name = getHushExportName(Call, Field->getASTContext()))
        Opts.customName = *Name;
    }
  }
  return Opts;
}

// Namespaces that get special handling (type lookup by decl name).
static const std::array<std::string, 1> SpecialNamespaces = {"glm"};

static bool isInSpecialNamespace(const std::string &Name) {
  for (const auto &NS : SpecialNamespaces)
    if (Name.find(NS) != std::string::npos)
      return true;
  return false;
}

CField ExportMatcher::resolveField(const clang::FieldDecl *Field,
                                   clang::ASTContext &Ctx) {
  CField Result;
  clang::QualType FieldType = Field->getType();

  auto FieldOpts = parseFieldAttr(Field);
  Result.name = FieldOpts.customName.empty() ? Field->getName().str()
                                             : FieldOpts.customName;

  // Private/protected → opaque
  if (Field->getAccess() != clang::AccessSpecifier::AS_public) {
    Result.isOpaque = true;
    Result.opaqueAlign = Ctx.getTypeAlign(FieldType) / 8;
    Result.opaqueSize = Ctx.getTypeSize(FieldType) / 8;
    Result.name = "m_member" + std::to_string(Field->getFieldIndex());
    return Result;
  }

  if (FieldOpts.ignore) {
    unsigned DiagID = Ctx.getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Field %0 is ignored, this is not supported when generating the "
        "bindings");
    Ctx.getDiagnostics().Report(Field->getLocation(), DiagID)
        << Field->getName();
    Result.isOpaque = true;
    Result.opaqueAlign = Ctx.getTypeAlign(FieldType) / 8;
    Result.opaqueSize = Ctx.getTypeSize(FieldType) / 8;
    return Result;
  }

  // Check if this is a registered type alias (e.g., ComponentCtor → typedef)
  if (const auto *TDT = FieldType->getAs<clang::TypedefType>()) {
    if (auto AliasRes =
            Registry_.lookup(TDT->getDecl()->getQualifiedNameAsString())) {
      Result.type = AliasRes->cType;
      return Result;
    }
  }

  // Function pointer fields (raw syntax or unregistered aliases)
  if (FieldType.getCanonicalType()->isFunctionPointerType()) {
    Result.type = buildFuncPointerCType(FieldType, /*ParamName=*/"", Ctx);
    Result.funcPointerDeclWithName =
        buildFuncPointerCType(FieldType, Result.name, Ctx).funcPointerDecl;
    return Result;
  }

  // Try the type registry
  auto Res = Registry_.resolve(FieldType);
  if (Res) {
    Result.type = Res->cType;
    return Result;
  }

  // Check for special namespace types by record decl name
  std::string FieldTypeStr = FieldType.getAsString();
  size_t TemplatePos = FieldTypeStr.find('<');
  if (TemplatePos != std::string::npos)
    FieldTypeStr = FieldTypeStr.substr(0, TemplatePos);

  if (isInSpecialNamespace(FieldTypeStr)) {
    if (auto Lookup = Registry_.lookup(FieldTypeStr)) {
      Result.type = CType::makeBuiltin(Lookup->cType.name);
      return Result;
    }
  }

  // Check record decl name
  if (const auto *RD = FieldType->getAsRecordDecl()) {
    std::string RdName = RD->getQualifiedNameAsString();
    if (auto Lookup = Registry_.lookup(RdName)) {
      Result.type = CType::makeBuiltin(Lookup->cType.name);
      return Result;
    }
  }

  // Fallback: use canonical type with :: → _
  std::string CanonName = stripTagKeywords(
      FieldType.getCanonicalType().getAsString(Ctx.getLangOpts()));
  std::replace(CanonName.begin(), CanonName.end(), ':', '_');
  Result.type = CType::makeBuiltin(CanonName);
  return Result;
}

void ExportMatcher::processClass(const clang::HushExportAttr *Attr,
                                 const clang::RecordDecl *D) {
  if (!Attr || !D->isCompleteDefinition() || D->isDependentType() ||
      D->isInvalidDecl())
    return;

  std::string CppName = D->getQualifiedNameAsString();
  if (ProcessedDecls_.count(CppName))
    return;

  auto Opts = parseExportAttr(Attr, D->getASTContext());
  if (Opts.ignore)
    return;

  std::string ExportName =
      Opts.customName.empty() ? makeDefaultExportName(CppName) : Opts.customName;

  ProcessedDecls_.insert(CppName);

  clang::ASTContext &Ctx = D->getASTContext();

  // Extract public type aliases from all classes (including opaque handles),
  // since aliases like EntityId may be referenced by exported functions.
  for (const auto *Decl : D->decls()) {
    const clang::TypedefNameDecl *TAD =
        llvm::dyn_cast<clang::TypedefNameDecl>(Decl);
    if (!TAD)
      continue;
    if (TAD->getAccess() != clang::AccessSpecifier::AS_public)
      continue;

    std::string AliasName = ExportName + "_" + TAD->getName().str();
    addTypeAlias(TAD, AliasName, Ctx);
  }

  if (Opts.asHandle) {
    CStruct s;
    s.name = ExportName;
    s.cppName = CppName;
    s.isOpaque = true;
    IR_.structs.push_back(s);
    Registry_.registerType(CppName, CType::makeOpaqueHandle(ExportName));
    return;
  }

  // Transparent struct
  CStruct s;
  s.name = ExportName;
  s.cppName = CppName;
  s.needsDestructor = !isPOD(D);
  s.cppUnqualifiedName = getUnqualifiedName(CppName);

  if (s.needsDestructor) {
    if (const auto *CRD = llvm::dyn_cast<clang::CXXRecordDecl>(D)) {
      if (!CRD->hasMoveConstructor() && !CRD->hasSimpleMoveConstructor()) {
        unsigned DiagID =
            Ctx.getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "when a type has a destructor, it must be move-constructible");
        Ctx.getDiagnostics().Report(D->getLocation(), DiagID);
      }
    }
  }

  for (const auto *Field : D->fields())
    s.fields.push_back(resolveField(Field, Ctx));

  IR_.structs.push_back(s);
  Registry_.registerType(CppName, CType::makeStruct(ExportName));
}

// ===== Function processing =====

ExportMatcher::ReturnInfo
ExportMatcher::resolveReturnType(const clang::FunctionDecl *D) {
  clang::QualType RetType = D->getReturnType();
  ReturnInfo Info;

  // Void
  if (RetType->isVoidType()) {
    Info.cType = CType::makeVoid();
    Info.mode = ReturnMode::Void;
    return Info;
  }

  ensureTypedefAliasRegistered(RetType, D->getASTContext());

  auto Res = Registry_.resolve(RetType);

  // Container return → callback
  if (Res && Res->isContainer) {
    Info.cType = CType::makeVoid();
    Info.mode = ReturnMode::Callback;
    CType CallbackType = Res->containerElementCType;
    CallbackType.name = normalizeStdIntType(CallbackType.name);
    Info.callbackInnerType = CallbackType.toString();
    return Info;
  }

  // Result<T, E> return. outcome_v2::unchecked has no stable C layout,
  // so it becomes a bool status plus (T* outValue, E* outError)
  // out-parameters.
  if (Res && Res->isResult) {
    clang::DiagnosticsEngine &Diags = D->getASTContext().getDiagnostics();

    // T and E are public-interface types. Enums there must be exported.
    if (auto ResultArgs = getResultTypeArgs(RetType)) {
      if (!ResultArgs->first->isVoidType())
        ensureEnumExported(ResultArgs->first, D->getLocation());
      ensureEnumExported(ResultArgs->second, D->getLocation());
    }

    // Validate the value type (nothing to validate for Result<void, E>).
    if (!Res->resultValueCppType.empty()) {
      bool ValueSupported =
          Res->resultValueCType.kind == CType::Builtin ||
          Res->resultValueCType.kind == CType::Enum ||
          Res->resultValueCType.kind == CType::Struct ||
          Res->resultValueCType.kind == CType::Pointer;
      if (!ValueSupported) {
        unsigned DiagID = Diags.getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Error: Result value type %0 is not supported in exported "
            "bindings\n");
        Diags.Report(D->getLocation(), DiagID) << Res->resultValueCppType;
      }
    }

    // Validate the error type: must be an enum or a builtin.
    bool ErrorSupported = Res->resultErrorCType.kind == CType::Builtin ||
                          Res->resultErrorCType.kind == CType::Enum;
    if (!ErrorSupported) {
      unsigned DiagID = Diags.getCustomDiagID(
          clang::DiagnosticsEngine::Error,
          "Error: Result error type %0 is not supported in exported "
          "bindings\n");
      Diags.Report(D->getLocation(), DiagID) << Res->resultErrorCppType;
    }

    Info.cType = CType::makeBuiltin("bool");
    Info.mode = ReturnMode::ResultOutParams;
    Info.resultValueCType = Res->resultValueCType;
    Info.resultErrorCType = Res->resultErrorCType;
    Info.resultValueIsEnum = Res->resultValueIsEnum;
    Info.resultErrorIsEnum = Res->resultErrorIsEnum;
    return Info;
  }

  // Enum return
  if (Res && Res->isEnum) {
    ensureEnumExported(RetType, D->getLocation());
    Info.cType = Res->cType;
    Info.mode = ReturnMode::StaticCastEnum;
    return Info;
  }

  // Function-pointer return. Same rationale as the parameter case: we need
  // a name-less function-pointer declaration with inner types resolved
  // through the registry, not the canonical desugared string.
  if (RetType.getCanonicalType()->isFunctionPointerType()) {
    bool UsedAlias = false;
    if (const auto *TDT = RetType->getAs<clang::TypedefType>()) {
      if (auto AliasRes = Registry_.lookup(
              TDT->getDecl()->getQualifiedNameAsString())) {
        Info.cType = AliasRes->cType;
        UsedAlias = true;
      }
    }
    if (!UsedAlias) {
      Info.cType = buildFuncPointerCType(RetType, /*ParamName=*/"",
                                         D->getASTContext());
    }
    Info.mode = ReturnMode::ReinterpretPtr;
    return Info;
  }

  // Pointer return
  if (RetType->isPointerType()) {
    // Resolve through registry for proper C name mapping
    if (Res) {
      Info.cType = Res->cType;
    } else {
      std::string TypeName = stripTagKeywords(RetType.getCanonicalType().getAsString(
          D->getASTContext().getPrintingPolicy()));
      std::replace(TypeName.begin(), TypeName.end(), ':', '_');
      Info.cType = CType::makeBuiltin(TypeName);
    }
    Info.mode = ReturnMode::ReinterpretPtr;
    return Info;
  }

  // Reference return — the old code doesn't really handle this as a
  // distinct case after the type is converted to pointer form; keep
  // ReinterpretPtr to match behavior.
  if (RetType->isReferenceType()) {
    clang::QualType Inner = RetType->getPointeeType();
    auto InnerRes = Registry_.resolve(Inner);
    if (InnerRes) {
      Info.cType = CType::makePointer(InnerRes->cType);
    } else {
      std::string TypeName = stripTagKeywords(RetType.getCanonicalType().getAsString(
          D->getASTContext().getPrintingPolicy()));
      std::replace(TypeName.begin(), TypeName.end(), ':', '_');
      std::replace(TypeName.begin(), TypeName.end(), '&', '*');
      Info.cType = CType::makeBuiltin(TypeName);
    }
    Info.mode = ReturnMode::ReinterpretPtr;
    return Info;
  }

  // Record/builtin/other value return
  std::string CppRetTypeName;
  if (Res) {
    Info.cType = Res->cType;
    CppRetTypeName = Res->cppName;
  } else {
    std::string TypeName = stripTagKeywords(RetType.getCanonicalType().getAsString(
        D->getASTContext().getPrintingPolicy()));
    CppRetTypeName = TypeName;
    std::replace(TypeName.begin(), TypeName.end(), ':', '_');
    Info.cType = CType::makeBuiltin(TypeName);
  }

  // Value return with destructor → PlacementNew
  if (hasDestructor(RetType)) {
    Info.mode = ReturnMode::PlacementNew;
    Info.cppReturnType = Info.cType.toString();
  } else {
    Info.mode = ReturnMode::DerefReinterpret;
  }

  return Info;
}

CParam ExportMatcher::resolveParam(const clang::FunctionDecl *D,
                                   const clang::ParmVarDecl *Param) {
  CParam Result;
  Result.name = Param->getNameAsString();
  clang::QualType ParamType = Param->getType();

  // If the parameter is a namespace-scope typedef (e.g., ObserverCallback_t),
  // make sure it's emitted as its own typedef and registered before we
  // resolve, so the alias name flows through instead of the desugared form.
  ensureTypedefAliasRegistered(ParamType, D->getASTContext());

  auto Res = Registry_.resolve(ParamType);

  // Container params (span, string_view) → split into data+size
  if (Res && Res->isContainer) {
    Result.isSpanParts = true;
    Result.mode = Res->isNullTerminatedStringView
                      ? PassMode::ZStringFromParts
                      : PassMode::SpanFromParts;
    Result.type = Res->containerElementCType;
    Result.type.name = normalizeStdIntType(Result.type.name);
    Result.cppContainerType = Res->containerCppType;
    Result.cppInnerRealType = Res->containerElementCppType;
    return Result;
  }

  // Result<T, E> is only supported as a return type
  if (Res && Res->isResult) {
    unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Error: Result<T, E> is only supported as a return type in "
        "exported bindings\n");
    D->getASTContext().getDiagnostics().Report(Param->getLocation(), DiagID);
    Result.type = CType::makeBuiltin("/* error */");
    Result.mode = PassMode::Direct;
    return Result;
  }

  // Enum params
  if (Res && Res->isEnum) {
    ensureEnumExported(ParamType, Param->getLocation());
    Result.type = Res->cType;
    Result.mode = PassMode::StaticCastEnum;
    Result.cppCastType = Res->enumCppType;
    return Result;
  }

  // Reference params → convert to pointer + DerefReinterpret
  if (ParamType->isReferenceType()) {
    clang::QualType Inner = ParamType->getPointeeType();
    auto InnerRes = Registry_.resolve(Inner.getUnqualifiedType());

    if (InnerRes) {
      CType InnerCType = InnerRes->cType;
      if (Inner.isConstQualified())
        InnerCType.isConst = true;
      Result.type = CType::makePointer(InnerCType);
    } else {
      std::string TypeName = stripTagKeywords(ParamType.getCanonicalType().getAsString(
          D->getASTContext().getLangOpts()));
      std::replace(TypeName.begin(), TypeName.end(), ':', '_');
      std::replace(TypeName.begin(), TypeName.end(), '&', '*');
      Result.type = CType::makeBuiltin(TypeName);
    }

    std::string RealType = stripTagKeywords(ParamType.getCanonicalType().getAsString(
        D->getASTContext().getLangOpts()));
    std::replace(RealType.begin(), RealType.end(), '&', '*');

    Result.mode = PassMode::DerefReinterpret;
    Result.cppCastType = RealType;
    return Result;
  }

  // Function-pointer params. Distinct from generic pointers so we can
  // (a) embed the parameter name inside "(*name)(...)" instead of
  //     appending it after the type (which is malformed C), and
  // (b) recursively resolve the inner return/parameter types through
  //     the registry, so typedefs like Entity::EntityId stay as
  //     Hush_Entity_EntityId rather than desugaring to unsigned long long.
  if (ParamType.getCanonicalType()->isFunctionPointerType()) {
    bool UsedAlias = false;
    if (const auto *TDT = ParamType->getAs<clang::TypedefType>()) {
      if (auto AliasRes = Registry_.lookup(
              TDT->getDecl()->getQualifiedNameAsString())) {
        Result.type = AliasRes->cType;
        UsedAlias = true;
      }
    }
    if (!UsedAlias) {
      Result.type = buildFuncPointerCType(ParamType, Result.name,
                                          D->getASTContext());
    }
    Result.mode = PassMode::Reinterpret;
    Result.cppCastType = stripTagKeywords(ParamType.getCanonicalType().getAsString(
        D->getASTContext().getLangOpts()));
    return Result;
  }

  // Pointer params
  if (ParamType->isPointerType()) {
    if (Res) {
      Result.type = Res->cType;
    } else {
      std::string TypeName = stripTagKeywords(ParamType.getCanonicalType().getAsString(
          D->getASTContext().getLangOpts()));
      std::replace(TypeName.begin(), TypeName.end(), ':', '_');
      Result.type = CType::makeBuiltin(TypeName);
    }

    std::string RealType = stripTagKeywords(ParamType.getCanonicalType().getAsString(
        D->getASTContext().getLangOpts()));

    Result.mode = PassMode::Reinterpret;
    Result.cppCastType = RealType;
    return Result;
  }

  // Record params (by value)
  if (ParamType->isRecordType()) {
    std::string CppName;

    if (Res) {
      Result.type = Res->cType;
      CppName = Res->cppName;
    } else {
      // Try to find via record decl name
      const auto *RD = ParamType->getAsRecordDecl();
      if (RD) {
        auto RdName = RD->getQualifiedNameAsString();
        if (auto Lookup = Registry_.lookup(RdName)) {
          Result.type = Lookup->cType;
          CppName = RdName;
        } else if (auto Exported = ensureRegistered(RD)) {
          Result.type = CType::makeStruct(*Exported);
          CppName = RdName;
        } else {
          // Error
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");
          D->getASTContext().getDiagnostics().Report(Param->getLocation(),
                                                     DiagID)
              << ParamType.getAsString();
          Result.type = CType::makeBuiltin("/* error */");
          Result.mode = PassMode::Direct;
          return Result;
        }
      }
    }

    // If the C name differs from the C++ name (e.g., glm::vec3 → Vector3),
    // we need a reinterpret_cast to convert between the layout-compatible types.
    if (Result.type.name != CppName) {
      Result.mode = PassMode::Reinterpret;
      Result.cppCastType = "const " + CppName + " &";
    } else {
      Result.mode = PassMode::Direct;
    }
    return Result;
  }

  // Builtin and everything else
  if (Res) {
    Result.type = Res->cType;
  } else {
    std::string TypeName = normalizeStdIntType(stripTagKeywords(
        ParamType.getCanonicalType().getAsString()));
    Result.type = CType::makeBuiltin(TypeName);
  }
  Result.mode = PassMode::Direct;
  return Result;
}

void ExportMatcher::processFunction(const clang::HushExportAttr *Attr,
                                    const clang::FunctionDecl *D) {
  if (!Attr)
    return;

  std::string CppName = D->getQualifiedNameAsString();

  // Deduplicate
  if (ProcessedDecls_.count("func:" + CppName))
    return;

  auto Opts = parseExportAttr(Attr, D->getASTContext());
  if (Opts.ignore)
    return;

  std::string ExportName =
      Opts.customName.empty() ? makeDefaultExportName(CppName) : Opts.customName;

  ProcessedDecls_.insert("func:" + CppName);

  CFunction Func;
  Func.name = ExportName;
  Func.cppName = CppName;

  // Member function?
  if (const auto *Parent = D->getParent();
      Parent && Parent->isRecord() && !D->isStatic()) {
    auto *Record = llvm::dyn_cast<clang::RecordDecl>(Parent);
    if (Record) {
      auto ClassExportName = ensureRegistered(Record);
      if (!ClassExportName) {
        unsigned DiagID =
            D->getASTContext().getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");
        D->getASTContext().getDiagnostics().Report(D->getLocation(), DiagID)
            << Record->getQualifiedNameAsString();
        return;
      }

      Func.isMemberFunction = true;
      Func.selfCType = *ClassExportName;
      Func.selfCppType = Record->getQualifiedNameAsString();

      // Extract method name by removing class prefix
      std::string MethodName = CppName;
      const std::string &ClassPrefix = Func.selfCppType;
      if (MethodName.find(ClassPrefix) == 0) {
        MethodName.erase(0, ClassPrefix.size());
        if (MethodName.size() >= 2 && MethodName[0] == ':' &&
            MethodName[1] == ':')
          MethodName.erase(0, 2);
      }
      Func.cppMethodName = MethodName;
    }
  }

  // Return type
  auto RetInfo = resolveReturnType(D);
  Func.returnType = RetInfo.cType;
  Func.returnMode = RetInfo.mode;
  Func.callbackInnerType = RetInfo.callbackInnerType;
  Func.cppReturnType = RetInfo.cppReturnType;
  Func.resultValueCType = RetInfo.resultValueCType;
  Func.resultErrorCType = RetInfo.resultErrorCType;
  Func.resultValueIsEnum = RetInfo.resultValueIsEnum;
  Func.resultErrorIsEnum = RetInfo.resultErrorIsEnum;

  // Parameters
  for (const auto *Param : D->parameters())
    Func.params.push_back(resolveParam(D, Param));

  IR_.functions.push_back(Func);
}
