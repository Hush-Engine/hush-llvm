#pragma once
#include "ReflectionModel.h"

#include "clang/AST/DeclCXX.h"

namespace hush_reflection {

/// Extracts a ClassModel from a CXXRecordDecl
/// This is the main entry point for extracting reflection data from a class declaration
///
/// @param Decl The CXXRecordDecl to extract the ClassModel from
/// @return The extracted ClassModel
ClassModel extractClassModel(const clang::CXXRecordDecl *Decl);

} // namespace hush_reflection
