#pragma once
#include "ReflectionModel.h"

#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Diagnostic.h"

namespace hush_reflection {

/// Extracts a ClassModel from a CXXRecordDecl
/// This is the main entry point for extracting reflection data from a class declaration
///
/// @param Decl The CXXRecordDecl to extract the ClassModel from
/// @param Diags Diagnostics engine used to report invalid hush annotation configurations
/// @return The extracted ClassModel
ClassModel extractClassModel(const clang::CXXRecordDecl *Decl,
                             clang::DiagnosticsEngine &Diags);

/// Extracts a ModuleInitFunction from a free function marked with
/// [[hush::module_init]].
///
/// @param Decl The FunctionDecl to extract from
/// @param Diags Diagnostics engine used to report invalid configurations
/// @return The extracted ModuleInitFunction, or std::nullopt when invalid
std::optional<ModuleInitFunction>
extractModuleInitFunction(const clang::FunctionDecl *Decl,
                          clang::DiagnosticsEngine &Diags);

} // namespace hush_reflection
