#pragma once

#include "clang/AST/ASTContext.h"
#include <clang/AST/Expr.h>
#include <optional>
#include <string>

std::optional<std::string> getHushExportName(const clang::CallExpr *CallExpr,
                                             clang::ASTContext &ASTContext);

// Functions that produce metadata macros for a particular exported symbol, this is useful for templates

/// @brief Produces a macro marker for a symbol that specifies parameters that could be converted to templated component ID searches at runtime
/// @details A marker macro output for this function that comes from the input
/// @code {.cpp}
/// // (On Entity.h)
/// [[hush::export(Hush::Export::queryCompIdParams("componentId"))]]
/// bool RemoveComponentRaw(EntityId componentId);
/// @endcode
/// Would produce:
/// @code {.c}
/// HUSH_QCOMP_ID(T_PAIR(T0, "componentId"))
/// extern bool Hush__Entity__RemoveComponentRaw(Hush__Entity *self, Hush__Entity_EntityId componentId);
/// @endcode
/// The binding generator's job is then to process that macro into a wrapper function at its target file
/// @code {.cs}
/// public bool RemoveComponent<T0>() {
///    return this.RemoveComponentRaw(QueryCompId<T0>());
/// }
/// @endcode
/// Where the query function can do a hash lookup based on T0's typename, an array lookup, whatever the language seems fit
std::optional<std::string> getHushQueryIdMarker(const clang::CallExpr* CallExpr,
                                                clang::ASTContext& ASTContext);

std::optional<std::string> getHushTypenameMarker(const clang::CallExpr* CallExpr,
                                                clang::ASTContext& ASTContext);

bool isHushExportIgnore(const clang::DeclRefExpr *CallExpr,
                                       clang::ASTContext &ASTContext);

bool isHushExportHandle(const clang::DeclRefExpr *CallExpr,
                                       clang::ASTContext &ASTContext);
