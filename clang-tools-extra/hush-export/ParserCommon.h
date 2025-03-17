#pragma once

#include <clang/AST/Expr.h>
#include <optional>
#include <string>

std::optional<std::string> getHushExportName(const clang::CallExpr *CallExpr,
                                             clang::ASTContext &ASTContext);

bool isHushExportIgnore(const clang::DeclRefExpr *CallExpr,
                                       clang::ASTContext &ASTContext);

bool isHushExportHandle(const clang::DeclRefExpr *CallExpr,
                                       clang::ASTContext &ASTContext);