//===-- ExportMatcher.h - AST matcher that produces CBindingIR directly ---===//
//
// Replaces HushBindingMatcher + IRBuilder. Walks the AST, resolves types
// through TypeRegistry, and populates CBindingIR in one pass.
//
//===----------------------------------------------------------------------===//

#ifndef HUSH_EXPORT_EXPORT_MATCHER_H
#define HUSH_EXPORT_EXPORT_MATCHER_H

#include "CBindingIR.h"
#include "TypeRegistry.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <set>

namespace hush {

/// Parsed attribute options common to all declarations.
struct ExportAttrOptions {
  std::string customName;
  bool ignore = false;
  bool asHandle = false;
};

class ExportMatcher : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  ExportMatcher();

  void run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

  /// Get the accumulated IR after all matches have been processed.
  const CBindingIR &getIR() const { return IR_; }
  CBindingIR takeIR() { return std::move(IR_); }

private:
  TypeRegistry Registry_;
  CBindingIR IR_;
  std::set<std::string> ProcessedDecls_;

  // ---- Setup ----
  void registerGlmTypes();

  // ---- Attribute parsing ----
  ExportAttrOptions parseExportAttr(const clang::HushExportAttr *Attr,
                                    clang::ASTContext &Ctx);

  // ---- Declaration handlers ----
  void processEnum(const clang::HushExportAttr *Attr,
                   const clang::EnumDecl *D);
  void processClass(const clang::HushExportAttr *Attr,
                    const clang::RecordDecl *D);
  void processFunction(const clang::HushExportAttr *Attr,
                       const clang::FunctionDecl *D);

  // ---- Type resolution helpers ----

  /// Resolve a function return type to CType + ReturnMode.
  struct ReturnInfo {
    CType cType;
    ReturnMode mode;
    std::string callbackInnerType; // For Callback mode
    std::string cppReturnType;     // For PlacementNew mode
  };
  ReturnInfo resolveReturnType(const clang::FunctionDecl *D);

  /// Resolve a function parameter to CParam.
  CParam resolveParam(const clang::FunctionDecl *D,
                      const clang::ParmVarDecl *Param);

  /// Resolve a class field to CField.
  CField resolveField(const clang::FieldDecl *Field,
                      clang::ASTContext &Ctx);

  /// Ensure a record type is registered. If it has [[hush_export]],
  /// process it on the fly. Returns the C-side name, or nullopt on failure.
  std::optional<std::string> ensureRegistered(const clang::RecordDecl *RD);

  /// Build a CType::FuncPointer whose return type and parameter types are
  /// resolved through the TypeRegistry. The C identifier ParamName is
  /// embedded between "(*" and ")". Pass an empty ParamName to build the
  /// type-only form (e.g., for typedef bodies).
  CType buildFuncPointerCType(clang::QualType FpType,
                              llvm::StringRef ParamName,
                              clang::ASTContext &Ctx);

  /// If the outermost typedef in Type's chain is a namespace-scope alias
  /// that hasn't been registered yet, register it and emit a CTypeAlias
  /// entry. Used to discover typedefs like `Hush::ObserverCallback_t`
  /// that are referenced by exported APIs but live outside any exported
  /// class. Idempotent.
  void ensureTypedefAliasRegistered(clang::QualType Type,
                                    clang::ASTContext &Ctx);

  /// Add a typedef alias to IR_ and Registry_. CName is the C-side name
  /// to emit (e.g., "Hush_Scene_EntityId" or "Hush__ObserverCallback_t").
  /// Shared between the per-class typedef loop and namespace-scope
  /// discovery via ensureTypedefAliasRegistered.
  void addTypeAlias(const clang::TypedefNameDecl *TAD,
                    const std::string &CName,
                    clang::ASTContext &Ctx);

  /// Get the default export name for a qualified C++ name (:: → _).
  static std::string makeDefaultExportName(const std::string &QualifiedName);

  /// Get the unqualified class name from a fully qualified name.
  static std::string getUnqualifiedName(const std::string &QualifiedName);

  /// Normalize std::uint32_t → uint32_t etc.
  static std::string normalizeStdIntType(const std::string &Type);

  /// Check if a type is a container (span, vector, string, string_view).
  static bool isContainerReturn(const TypeResolution &Res);
};

} // namespace hush

#endif // HUSH_EXPORT_EXPORT_MATCHER_H
