#include "ParserCommon.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>

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

bool isHushExportIgnore(const clang::DeclRefExpr *CallExpr,
                        clang::ASTContext &ASTContext) {
  return CallExpr->getDecl()->getName() == "ignore";
}

bool isHushExportHandle(const clang::DeclRefExpr *CallExpr,
                        clang::ASTContext &ASTContext) {
  return CallExpr->getDecl()->getName() == "asHandle";
}
