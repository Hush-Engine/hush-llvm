#include "ParserCommon.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <optional>
#include <vector>

bool hushArgStringify(const clang::Expr* Arg, llvm::StringRef& OutStr) {
    if (Arg == nullptr) {
        return false;
    }

    const clang::StringLiteral* ArgStringLiteral = llvm::dyn_cast<clang::StringLiteral>(Arg);
    if (ArgStringLiteral == nullptr) {
        return false;
    }

    OutStr = ArgStringLiteral->getString();
    return true;
}

std::optional<std::string> getHushExportName(const clang::CallExpr *CallExpr,
                                             clang::ASTContext &ASTContext) {
  const clang::FunctionDecl *ArgFunction = CallExpr->getDirectCallee();
  if (ArgFunction != nullptr && ArgFunction->getName() == "name") {
    // Get the argument value
    const clang::Expr *ArgValue = CallExpr->getArg(0);

    if (ArgValue != nullptr) {
      if (const clang::StringLiteral *ArgStringLiteral =
              llvm::dyn_cast<clang::StringLiteral>(ArgValue)) {
        return ArgStringLiteral->getString().str();
      }
    }

    clang::DiagnosticsEngine &DiagEngine = ASTContext.getDiagnostics();

    unsigned DiagID = DiagEngine.getCustomDiagID(
        clang::DiagnosticsEngine::Error,
        "Hush::Export::name should have a string literal argument");

    DiagEngine.Report(ArgValue->getExprLoc(), DiagID);
  }

  return std::nullopt;
}


std::optional<std::string> getHushQueryIdMarker(const clang::CallExpr* CallExpr,
                                            clang::ASTContext& ASTContext) {
    const clang::FunctionDecl* ArgFunction = CallExpr->getDirectCallee();
    if (ArgFunction == nullptr || ArgFunction->getName() != "queryCompIdParams") {
        return std::nullopt;
    }

    uint32_t ParsedArgs = 0;
    std::string Marker = "HUSH_QCOMP_ID(";

    // The queryCompIdParams function has variadic args, we must parse all of them
    uint32_t Argc = CallExpr->getNumArgs();
    for (uint32_t Idx = 0; Idx < Argc; Idx++) {
        const clang::Expr* Argv = CallExpr->getArg(Idx);
        llvm::StringRef ArgName{};
        if (!hushArgStringify(Argv, ArgName)) {
            break;
        }
        Marker += "T_PAIR(T";
        Marker += llvm::utostr(Idx);
        Marker += ", \"";
        Marker += ArgName;
        Marker += "\")";
        Marker +=  ", ";
        ParsedArgs++;
    }

    if (ParsedArgs == 0) {
        clang::DiagnosticsEngine &DiagEngine = ASTContext.getDiagnostics();

        unsigned DiagID = DiagEngine.getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Hush::Export::queryCompIdParams should have one or more string literal arguments");

        DiagEngine.Report(CallExpr->getExprLoc(), DiagID);

        return std::nullopt;
    }

    // Take out the trailing comma
    Marker.resize(Marker.size() - 1);

    Marker += ")";

    return Marker;
}


std::optional<std::string> getHushTypenameMarker(const clang::CallExpr* CallExpr,
                                                clang::ASTContext& ASTContext) {
    (void)CallExpr;
    (void)ASTContext;
    // NYI:
    return std::nullopt;
}

bool isHushExportIgnore(const clang::DeclRefExpr *CallExpr,
                        clang::ASTContext &ASTContext) {
  return CallExpr->getDecl()->getName() == "ignore";
}

bool isHushExportHandle(const clang::DeclRefExpr *DeclRef) {
  return DeclRef->getDecl()->getName() == "asHandle";
}

bool isHushTemplatable(const clang::DeclRefExpr *CallExpr,
                       clang::ASTContext &ASTContext) {
    return CallExpr->getDecl()->getName() == "templatable";
}
