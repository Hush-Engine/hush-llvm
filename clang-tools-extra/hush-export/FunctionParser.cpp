
#include "FunctionParser.h"

#include "ParserCommon.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/Basic/SourceManager.h>

using namespace std::string_view_literals;

static bool isSpecialType(std::string_view Type) {
  for (const auto &SpecialType : SpecialTypes) {
    if (Type.find(SpecialType) != std::string::npos) {
      return true;
    }
  }

  return false;
}

static bool isSpecialReturnType(std::string_view Type) {
  for (const auto &SpecialType : SpecialReturnTypes) {
    if (Type.find(SpecialType) != std::string::npos) {
      return true;
    }
  }

  return false;
}

static bool isSpecialType(const clang::QualType &Type) {
  return isSpecialType(Type.getAsString());
}

static bool isSpecialReturnType(const clang::QualType &Type) {
  return isSpecialReturnType(Type.getAsString());
}

bool processPointerParam(
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {
  // Check if it is a pointer to a record
  auto PointeeType = Param->getType()->getPointeeType();

  // If the pointee is a record, we need to check if it is already parsed
  if (PointeeType->isRecordType()) {
    auto FullyQualifiedName = PointeeType.getAsString();

    // Okay, we have a class, check if it is already parsed
    auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
    if (AlreadyExported == ParsedClassesMap.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(Param->getLocation(), DiagID) << FullyQualifiedName;
      return false;
    }

    FuncParam.Type = AlreadyExported->second->ExportedName;
  } else if (PointeeType->isBuiltinType()) {
    FuncParam.Type = PointeeType.getAsString();
  }

  return true;
}
bool processRecordTypeParam(
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {
  auto FullyQualifiedName = Param->getType().getAsString();

  // First, check if it as a special type
  if (FullyQualifiedName.find("std::span") != std::string::npos ||
      FullyQualifiedName.find("std::string_view")) {

    // Get the inner type
    auto InnerType = Param->getType()
                         ->getAs<clang::TemplateSpecializationType>()
                         ->template_arguments()
                         .front()
                         .getAsType();

    FuncParam.InnerType.IsConst = InnerType.isConstQualified();
    FuncParam.InnerType.IsPointer = InnerType->isPointerType();
    FuncParam.InnerType.IsReference = InnerType->isReferenceType();

    // If the inner type is a record, we need to check if it is already parsed
    if (InnerType->isRecordType()) {
      auto InnerFullyQualifiedName = InnerType.getAsString();

      // Okay, we have a class, check if it is already parsed
      auto AlreadyExported = ParsedClassesMap.find(InnerFullyQualifiedName);
      if (AlreadyExported == ParsedClassesMap.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(Param->getLocation(), DiagID)
            << InnerFullyQualifiedName;
        return false;
      }

      FuncParam.InnerType.Type = AlreadyExported->second->ExportedName;
    } else {
      FuncParam.InnerType.Type = InnerType.getAsString();
    }

    FuncParam.Type = FullyQualifiedName;

  } else {
    // Okay, we have a class, check if it is already parsed
    auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
    if (AlreadyExported == ParsedClassesMap.end()) {

      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(Param->getLocation(), DiagID) << FullyQualifiedName;
      return false;
    }

    // We export it.
    FuncParam.Type = AlreadyExported->second->ExportedName;
  }
  return true;
}
bool processFuncReturnType(
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    const clang::FunctionDecl *D, FunctionInfo &FuncInfo,
    clang::QualType ReturnType) {
  if (ReturnType->isBuiltinType()) {
    FuncInfo.ReturnType.Type = ReturnType.getAsString();
  } else if (ReturnType->isRecordType()) {
    auto FullyQualifiedName = ReturnType.getAsString();

    // First, check if it as a special type
    if (FullyQualifiedName.find("std::span") != std::string::npos ||
        FullyQualifiedName.find("std::string_view") != std::string::npos ||
        FullyQualifiedName.find("std::vector") != std::string::npos ||
        FullyQualifiedName.find("std::string") != std::string::npos) {
      // Get the inner type
      auto InnerType = ReturnType->getAs<clang::TemplateSpecializationType>()
                           ->template_arguments()
                           .front()
                           .getAsType();

      // If the inner type is a record, we need to check if it is already
      // parsed
      if (InnerType->isRecordType()) {
        auto InnerFullyQualifiedName = InnerType.getAsString();

        // Okay, we have a class, check if it is already parsed
        auto AlreadyExported = ParsedClassesMap.find(InnerFullyQualifiedName);
        if (AlreadyExported == ParsedClassesMap.end()) {
          unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

          clang::DiagnosticsEngine &DiagEngine =
              D->getASTContext().getDiagnostics();
          DiagEngine.Report(D->getLocation(), DiagID)
              << InnerFullyQualifiedName;
          return false;
        }

        FuncInfo.ReturnType.InnerType = AlreadyExported->second->ExportedName;
      } else {
        FuncInfo.ReturnType.InnerType = InnerType.getAsString();
      }

      // Remove everything after the first <
      auto Pos = FullyQualifiedName.find('<');
      if (Pos != std::string::npos) {
        FullyQualifiedName.erase(Pos);
      }

      FuncInfo.ReturnType.Type = FullyQualifiedName;
    } else {
      // Okay, we have a class, check if it is already parsed
      auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
      if (AlreadyExported == ParsedClassesMap.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
        return false;
      }

      // We export it.
      FuncInfo.ReturnType.Type = AlreadyExported->second->ExportedName;
    }
  }

  return true;
}
void processFunctionDecl(
    std::map<std::string, std::shared_ptr<ExportedClass>> &ParsedClassesMap,
    std::vector<FunctionInfo> &FunctionInfos,
    const clang::HushExportAttr *HushExportAttr, const clang::FunctionDecl *D) {

  if (!HushExportAttr) {
    return;
  }

  // Check if we already have this function
  for (const auto &Func : FunctionInfos) {
    if (Func.Name == D->getQualifiedNameAsString()) {
      return;
    }
  }

  std::string ExportName;
  std::string FullFunctionName = D->getQualifiedNameAsString();

  auto *AttributeArgsBegin = HushExportAttr->exportConfig_begin();
  auto *AttributeArgsEnd = HushExportAttr->exportConfig_end();

  for (clang::Expr **ArgExpr = AttributeArgsBegin; ArgExpr != AttributeArgsEnd;
       ++ArgExpr) {
    clang::DeclRefExpr *ArgRef =
        llvm::dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());

    if (ArgRef != nullptr) {
      if (isHushExportIgnore(ArgRef, D->getASTContext())) {
        // Ignore this function.
        return;
      }
    }

    clang::CallExpr *ArgCall =
        llvm::dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
    if (ArgCall != nullptr) {
      if (auto Name = getHushExportName(ArgCall, D->getASTContext());
          Name.has_value()) {
        ExportName = *Name;
      }
    }
  }

  if (ExportName.empty()) {
    ExportName = D->getQualifiedNameAsString();
    // Replace :: with _
    std::replace(ExportName.begin(), ExportName.end(), ':', '_');
  }

  auto FuncInfo = FunctionInfo{};
  FuncInfo.Name = FullFunctionName;
  FuncInfo.ExportedName = ExportName;

  // Check if a class owns this function
  if (const auto *Parent = D->getParent(); Parent && Parent->isRecord()) {
    auto *Record = llvm::dyn_cast<clang::RecordDecl>(Parent);
    auto FullyQualifiedName = Record->getQualifiedNameAsString();

    auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
    if (AlreadyExported == ParsedClassesMap.end()) {
      unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          D->getASTContext().getDiagnostics();
      DiagEngine.Report(D->getLocation(), DiagID) << FullyQualifiedName;
      return;
    }

    FuncInfo.ContainingClass = AlreadyExported->second;
  }

  auto ReturnType = D->getReturnType();
  if (!processFuncReturnType(ParsedClassesMap, D, FuncInfo, ReturnType))
    return;

  for (const clang::ParmVarDecl *Param : D->parameters()) {
    // If the parameter is a record, we need to check if it is already parsed
    auto FuncParam = FunctionParam{};
    FuncParam.Name = Param->getNameAsString();
    FuncParam.IsConst = Param->getType().isConstQualified();
    FuncParam.IsPointer = Param->getType()->isPointerType();
    FuncParam.IsReference = Param->getType()->isReferenceType();

    // First, check if the parameter has a HushExport attribute
    auto *ParamHushExportAttr = Param->getAttr<clang::HushExportAttr>();

    if (ParamHushExportAttr) {
      AttributeArgsBegin = ParamHushExportAttr->exportConfig_begin();
      AttributeArgsEnd = ParamHushExportAttr->exportConfig_end();

      for (clang::Expr **ArgExpr = AttributeArgsBegin;
           ArgExpr != AttributeArgsEnd; ++ArgExpr) {
        clang::DeclRefExpr *ArgRef =
            llvm::dyn_cast<clang::DeclRefExpr>((*ArgExpr)->IgnoreImplicit());

        if (ArgRef != nullptr) {
          if (isHushExportIgnore(ArgRef, D->getASTContext())) {
            // Ignore this parameter.
            continue;
          }
        }

        clang::CallExpr *ArgCall =
            llvm::dyn_cast<clang::CallExpr>((*ArgExpr)->IgnoreImplicit());
        if (ArgCall != nullptr) {
          if (auto Name = getHushExportName(ArgCall, D->getASTContext());
              Name.has_value()) {
            FuncParam.Name = *Name;
          }
        }
      }
    }

    if (Param->getType()->isRecordType()) {
      if (!processRecordTypeParam(ParsedClassesMap, D, Param, FuncParam))
        return;
    } else if (Param->getType()->isPointerType()) {
      if (!processPointerParam(ParsedClassesMap, D, Param, FuncParam)) {
        return;
      }
    } else if (Param->getType()->isBuiltinType()) {
      FuncParam.Type = Param->getType().getAsString();
    }

    // Add the parameter
    FuncInfo.Parameters.push_back(FuncParam);
  }

  FunctionInfos.push_back(FuncInfo);
}