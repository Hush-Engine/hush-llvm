//===-- TypeRegistry.cpp - Central type resolution for C bindings ---------===//

#include "TypeRegistry.h"

#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"

using namespace hush;

/// Get the canonical type string with C++ tag keywords (class/struct/enum)
/// stripped. Clang's canonical printer adds these but they break C output
/// and C++ wrapper code.
static std::string getCleanCanonicalName(clang::QualType Type) {
  std::string Name = Type.getCanonicalType().getAsString();
  for (llvm::StringRef Prefix : {"class ", "struct ", "enum "}) {
    if (llvm::StringRef(Name).starts_with(Prefix))
      Name.erase(0, Prefix.size());
  }
  return Name;
}

// ===== TypeRegistry =====

void TypeRegistry::registerType(const std::string &cppName, CType cType,
                                bool isEnum) {
  types_[cppName] = RegisteredType{std::move(cType), isEnum};
}

std::optional<TypeResolution>
TypeRegistry::lookup(const std::string &cppName) const {
  auto It = types_.find(cppName);
  if (It == types_.end())
    return std::nullopt;

  TypeResolution Res;
  Res.cType = It->second.cType;
  Res.cppName = cppName;
  Res.isEnum = It->second.isEnum;
  if (Res.isEnum)
    Res.enumCppType = cppName;
  return Res;
}

bool TypeRegistry::isRegistered(const std::string &cppName) const {
  return types_.find(cppName) != types_.end();
}

void TypeRegistry::addTranslator(std::unique_ptr<TypeTranslator> Translator) {
  translators_.push_back(std::move(Translator));
}

std::optional<TypeResolution>
TypeRegistry::resolve(clang::QualType Type) const {
  // Try each translator in order
  for (const auto &Translator : translators_) {
    if (Translator->canTranslate(Type, *this))
      return Translator->translate(Type, *this);
  }

  // Fallback: try sugar-preserved name (handles typedefs like glm::vec3)
  // Strip qualifiers so "const glm::vec3" matches the "glm::vec3" key.
  if (auto Res = lookup(Type.getUnqualifiedType().getAsString()))
    return Res;

  // Last resort: try the canonical (desugared) type string
  return lookup(Type.getCanonicalType().getAsString());
}

std::vector<std::string> TypeRegistry::registeredTypeNames() const {
  std::vector<std::string> names;
  names.reserve(types_.size());
  for (const auto &Pair : types_)
    names.push_back(Pair.first);
  return names;
}

// ===== TypeAliasTranslator =====

bool TypeAliasTranslator::canTranslate(clang::QualType Type,
                                       const TypeRegistry &Registry) const {
  // Walk the typedef chain: Scene::EntityId → Entity::EntityId → uint64_t
  // Stop at the first registered alias.
  clang::QualType Cur = Type;
  while (const auto *TDT = Cur->getAs<clang::TypedefType>()) {
    if (Registry.isRegistered(TDT->getDecl()->getQualifiedNameAsString()))
      return true;
    // Desugar one level and try the next typedef in the chain
    clang::QualType Desugared = TDT->desugar();
    if (Desugared == Cur)
      break;
    Cur = Desugared;
  }
  return false;
}

TypeResolution
TypeAliasTranslator::translate(clang::QualType Type,
                               const TypeRegistry &Registry) const {
  clang::QualType Cur = Type;
  while (const auto *TDT = Cur->getAs<clang::TypedefType>()) {
    std::string QualName = TDT->getDecl()->getQualifiedNameAsString();
    if (auto Res = Registry.lookup(QualName))
      return *Res;
    clang::QualType Desugared = TDT->desugar();
    if (Desugared == Cur)
      break;
    Cur = Desugared;
  }
  llvm_unreachable("canTranslate should have verified a match exists");
}

// ===== BuiltinTranslator =====

bool BuiltinTranslator::canTranslate(clang::QualType Type,
                                      const TypeRegistry &) const {
  return Type->isBuiltinType();
}

TypeResolution BuiltinTranslator::translate(clang::QualType Type,
                                            const TypeRegistry &) const {
  TypeResolution Res;
  // Prefer the sugar-preserved name so uint32_t stays uint32_t
  // (instead of desugaring to "unsigned int").
  std::string Name = Type.getUnqualifiedType().getAsString();

  // C spells bool as _Bool; use <stdbool.h>'s "bool" instead.
  if (Name == "_Bool")
    Name = "bool";

  // Strip std:: prefix from integer types (std::uint32_t → uint32_t).
  if (Name.substr(0, 5) == "std::")
    Name = Name.substr(5);

  Res.cppName = Name;
  Res.cType = CType::makeBuiltin(Name);
  return Res;
}

// ===== EnumTranslator =====

bool EnumTranslator::canTranslate(clang::QualType Type,
                                   const TypeRegistry &) const {
  return Type->isEnumeralType();
}

TypeResolution EnumTranslator::translate(clang::QualType Type,
                                         const TypeRegistry &Registry) const {
  // Get the declaration's qualified name (without the "enum" keyword prefix
  // that getCanonicalType().getAsString() includes)
  const auto *ET = Type->getAs<clang::EnumType>();
  std::string CppName = ET->getDecl()->getQualifiedNameAsString();

  // Check if already registered (e.g., with a custom export name)
  if (auto Registered = Registry.lookup(CppName))
    return *Registered;

  // Default: replace :: with _
  std::string CName = CppName;
  std::replace(CName.begin(), CName.end(), ':', '_');

  TypeResolution Res;
  Res.cType = CType::makeEnum(CName);
  Res.cppName = CppName;
  Res.isEnum = true;
  Res.enumCppType = CppName;
  return Res;
}

// ===== PointerTranslator =====

bool PointerTranslator::canTranslate(clang::QualType Type,
                                      const TypeRegistry &) const {
  return Type->isPointerType();
}

TypeResolution PointerTranslator::translate(clang::QualType Type,
                                            const TypeRegistry &Registry) const {
  clang::QualType Pointee = Type->getPointeeType();
  bool PointeeIsConst = Pointee.isConstQualified();

  // Resolve the inner type
  clang::QualType UnqualPointee = Pointee.getUnqualifiedType();
  auto InnerRes = Registry.resolve(UnqualPointee);

  if (!InnerRes) {
    // If we can't resolve the inner type, return a raw string fallback
    TypeResolution Res;
    std::string TypeName = getCleanCanonicalName(Type);
    std::string CName = TypeName;
    std::replace(CName.begin(), CName.end(), ':', '_');
    Res.cType = CType::makeBuiltin(CName); // Fallback as opaque
    Res.cppName = TypeName;
    return Res;
  }

  TypeResolution Res;
  CType InnerCType = InnerRes->cType;
  if (PointeeIsConst)
    InnerCType.isConst = true;

  Res.cType = CType::makePointer(std::move(InnerCType));
  Res.cppName = getCleanCanonicalName(Type);
  // Propagate enum-ness for pointer-to-enum (rare but possible)
  Res.isEnum = InnerRes->isEnum;
  Res.enumCppType = InnerRes->enumCppType;
  return Res;
}

// ===== ReferenceTranslator =====

bool ReferenceTranslator::canTranslate(clang::QualType Type,
                                        const TypeRegistry &) const {
  return Type->isReferenceType();
}

TypeResolution
ReferenceTranslator::translate(clang::QualType Type,
                               const TypeRegistry &Registry) const {
  clang::QualType Pointee = Type->getPointeeType();
  bool PointeeIsConst = Pointee.isConstQualified();

  // Resolve the inner type
  clang::QualType UnqualPointee = Pointee.getUnqualifiedType();
  auto InnerRes = Registry.resolve(UnqualPointee);

  if (!InnerRes) {
    TypeResolution Res;
    std::string TypeName = Type.getCanonicalType().getAsString();
    std::string CName = TypeName;
    std::replace(CName.begin(), CName.end(), ':', '_');
    // Replace & with * for C compatibility
    std::replace(CName.begin(), CName.end(), '&', '*');
    Res.cType = CType::makeBuiltin(CName);
    Res.cppName = TypeName;
    return Res;
  }

  // Convert reference to pointer
  TypeResolution Res;
  CType InnerCType = InnerRes->cType;
  if (PointeeIsConst)
    InnerCType.isConst = true;

  Res.cType = CType::makePointer(std::move(InnerCType));
  Res.cppName = getCleanCanonicalName(Type);
  Res.isEnum = InnerRes->isEnum;
  Res.enumCppType = InnerRes->enumCppType;
  return Res;
}

// ===== ContainerTranslator =====

/// Check if a QualType is a specific template class by name.
static bool isTemplateNamed(clang::QualType Type,
                            llvm::ArrayRef<llvm::StringRef> Names) {
  const auto *TST = Type->getAs<clang::TemplateSpecializationType>();
  if (!TST)
    return false;

  clang::TemplateName TN = TST->getTemplateName();
  auto *TD = TN.getAsTemplateDecl();
  if (!TD)
    return false;

  std::string QualName = TD->getQualifiedNameAsString();
  for (auto Name : Names) {
    if (QualName == Name)
      return true;
  }
  return false;
}

static const llvm::StringRef ContainerNames[] = {
    "std::span", "std::vector", "std::basic_string", "std::basic_string_view"};

bool ContainerTranslator::canTranslate(clang::QualType Type,
                                        const TypeRegistry &) const {
  return isTemplateNamed(Type, ContainerNames);
}

TypeResolution
ContainerTranslator::translate(clang::QualType Type,
                               const TypeRegistry &Registry) const {
  const auto *TST = Type->getAs<clang::TemplateSpecializationType>();
  assert(TST && "canTranslate should have verified this");

  // Get the inner/element type (first template argument)
  clang::QualType ElementType =
      TST->template_arguments().front().getAsType();

  std::string ElementCppType = getCleanCanonicalName(ElementType);

  // Resolve the element type through the registry
  auto ElementRes = Registry.resolve(ElementType);

  CType ElementCType;
  if (ElementRes) {
    ElementCType = ElementRes->cType;
  } else {
    // Fallback: use the canonical name with :: replaced
    std::string CName = ElementCppType;
    std::replace(CName.begin(), CName.end(), ':', '_');
    ElementCType = CType::makeBuiltin(CName);
  }

  // String views expose read-only character storage even though their first
  // template argument is the unqualified character type.
  if (isTemplateNamed(Type, {"std::basic_string_view"})) {
    ElementCType.isConst = true;
    ElementCppType = "const " + ElementCppType;
  }

  TypeResolution Res;
  // The C-side type for a container return is void (callback handles it).
  // For parameters, it expands to (T* data, size_t size).
  // The caller (function builder) decides based on context.
  // We set the cType to void as a placeholder — the function builder
  // will override it appropriately.
  Res.cType = CType::makeVoid();
  Res.cppName = getCleanCanonicalName(Type);
  Res.isContainer = true;
  Res.containerElementCType = ElementCType;
  Res.containerElementCppType = ElementCppType;
  Res.containerCppType = Res.cppName;

  return Res;
}

// ===== ZStringViewTranslator =====

/// The fully qualified name of the engine's null-terminated string view.
static constexpr llvm::StringRef ZStringViewCppName =
    "Hush::NullTerminatedStringView";

bool ZStringViewTranslator::canTranslate(clang::QualType Type,
                                         const TypeRegistry &) const {
  // NullTerminatedStringView is a plain (non-template) record. Matching on
  // the record decl name also catches aliases such as `zstring_view`,
  // since getAsRecordDecl() looks through typedef sugar.
  const clang::RecordDecl *RD = Type->getAsRecordDecl();
  return RD && RD->getQualifiedNameAsString() == ZStringViewCppName;
}

TypeResolution
ZStringViewTranslator::translate(clang::QualType Type,
                                 const TypeRegistry &) const {
  TypeResolution Res;
  // Same placeholder convention as ContainerTranslator. The function
  // builder expands this into (const char* data, size_t size) params.
  Res.cType = CType::makeVoid();
  Res.cppName = ZStringViewCppName.str();
  Res.isContainer = true;
  Res.isNullTerminatedStringView = true;
  Res.containerElementCType = CType::makeBuiltin("const char");
  Res.containerElementCppType = "const char";
  Res.containerCppType = ZStringViewCppName.str();
  return Res;
}

// ===== ResultTranslator =====

/// Match Type against outcome's basic_result. We match the canonical decl
/// instead of the written sugar so nested aliases like
/// `using Result<T> = Hush::Result<T, EError>;` are caught too.
static const clang::ClassTemplateSpecializationDecl *
getAsResultSpecialization(clang::QualType Type) {
  // getAsCXXRecordDecl() looks through typedef/alias sugar.
  const auto *CTSD = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
      Type->getAsCXXRecordDecl());
  if (!CTSD || CTSD->getName() != "basic_result")
    return nullptr;
  // The namespace is versioned (outcome_v2_<commit>). Match the prefix.
  const auto *NS =
      llvm::dyn_cast<clang::NamespaceDecl>(CTSD->getDeclContext());
  if (!NS || !NS->getName().starts_with("outcome_v2"))
    return nullptr;
  return CTSD;
}

std::optional<std::pair<clang::QualType, clang::QualType>>
hush::getResultTypeArgs(clang::QualType Type) {
  const auto *CTSD = getAsResultSpecialization(Type);
  if (!CTSD)
    return std::nullopt;
  const auto &Args = CTSD->getTemplateArgs();
  // basic_result<T, E, NoValuePolicy>. (T, E) are always the first two.
  if (Args.size() < 2 || Args[0].getKind() != clang::TemplateArgument::Type ||
      Args[1].getKind() != clang::TemplateArgument::Type)
    return std::nullopt;
  return std::make_pair(Args[0].getAsType(), Args[1].getAsType());
}

bool ResultTranslator::canTranslate(clang::QualType Type,
                                    const TypeRegistry &) const {
  return getAsResultSpecialization(Type) != nullptr;
}

TypeResolution ResultTranslator::translate(clang::QualType Type,
                                           const TypeRegistry &Registry) const {
  auto ResultArgs = hush::getResultTypeArgs(Type);
  assert(ResultArgs && "canTranslate should have verified this");

  TypeResolution Res;
  // The C function returns the bool status. Value and error go through
  // out-parameters.
  Res.cType = CType::makeBuiltin("bool");
  Res.cppName = getCleanCanonicalName(Type);
  Res.isResult = true;

  clang::QualType ValueType = ResultArgs->first;
  clang::QualType ErrorType = ResultArgs->second;

  if (!ValueType->isVoidType()) {
    if (auto ValueRes = Registry.resolve(ValueType)) {
      Res.resultValueCType = ValueRes->cType;
      Res.resultValueIsEnum = ValueRes->isEnum;
    }
    Res.resultValueCppType = getCleanCanonicalName(ValueType);
  }

  if (auto ErrorRes = Registry.resolve(ErrorType)) {
    Res.resultErrorCType = ErrorRes->cType;
    Res.resultErrorIsEnum = ErrorRes->isEnum;
  }
  Res.resultErrorCppType = getCleanCanonicalName(ErrorType);

  return Res;
}

// ===== RecordTranslator =====

bool RecordTranslator::canTranslate(clang::QualType Type,
                                     const TypeRegistry &Registry) const {
  if (!Type->isRecordType())
    return false;

  // Check if this record is registered in the registry
  const auto *RD = Type->getAsRecordDecl();
  if (!RD)
    return false;

  return Registry.isRegistered(RD->getQualifiedNameAsString());
}

TypeResolution
RecordTranslator::translate(clang::QualType Type,
                            const TypeRegistry &Registry) const {
  const auto *RD = Type->getAsRecordDecl();
  assert(RD && "canTranslate should have verified this");

  std::string CppName = RD->getQualifiedNameAsString();
  auto Registered = Registry.lookup(CppName);
  assert(Registered && "canTranslate should have verified registration");

  return *Registered;
}

// ===== Factory =====

std::unique_ptr<TypeRegistry> hush::createDefaultRegistry() {
  auto Registry = std::make_unique<TypeRegistry>();

  // Order matters: more specific translators first.
  // 0. Type aliases (registered using/typedef names take priority over all)
  Registry->addTranslator(std::make_unique<TypeAliasTranslator>());
  // 1. Containers (before records, since containers are also records)
  Registry->addTranslator(std::make_unique<ContainerTranslator>());
  // 1a. NullTerminatedStringView (non-template record, container-like)
  Registry->addTranslator(std::make_unique<ZStringViewTranslator>());
  // 1b. Result<T, E> (template record, return-type-only expansion)
  Registry->addTranslator(std::make_unique<ResultTranslator>());
  // 2. Builtins
  Registry->addTranslator(std::make_unique<BuiltinTranslator>());
  // 3. Enums
  Registry->addTranslator(std::make_unique<EnumTranslator>());
  // 4. References (before pointers, since ref→ptr conversion is specific)
  Registry->addTranslator(std::make_unique<ReferenceTranslator>());
  // 5. Pointers
  Registry->addTranslator(std::make_unique<PointerTranslator>());
  // 6. Records (general struct/class lookup)
  Registry->addTranslator(std::make_unique<RecordTranslator>());

  return Registry;
}
