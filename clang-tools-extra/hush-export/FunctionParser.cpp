
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
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
    const clang::FunctionDecl *D, const clang::ParmVarDecl *Param,
    FunctionParam &FuncParam) {
  // Check if it is a pointer to a record
  auto PointeeType = Param->getType()->getPointeeType();

  // If the pointee is a record, we need to check if it is already parsed
  if (PointeeType->isRecordType()) {
    // Check if it is elaborated
    if (const auto *Elaborated = PointeeType->getAs<clang::ElaboratedType>(); Elaborated != nullptr) {
      PointeeType = Elaborated->getNamedType();
    }

    auto FullyQualifiedName = PointeeType.getAsString();

    if (FullyQualifiedName.find("struct ") != std::string::npos) {
      FullyQualifiedName = FullyQualifiedName.substr(7);
    } else if (FullyQualifiedName.find("class ") != std::string::npos) {
      FullyQualifiedName = FullyQualifiedName.substr(6);
    }

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

    FuncParam.Type = AlreadyExported->second.ExportedName;
    FuncParam.RealType = AlreadyExported->second.Name;
  } else if (PointeeType->isBuiltinType()) {
    FuncParam.Type = PointeeType.getAsString();
  }

  return true;
}
bool processRecordTypeParam(
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
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

      FuncParam.InnerType.Type = AlreadyExported->second.ExportedName;
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
    FuncParam.Type = AlreadyExported->second.ExportedName;
    FuncParam.RealType = AlreadyExported->second.Name;
  }
  return true;
}

bool processPointerRet(std::map<std::string, ExportedTypeInfo> &ExportedTypes,
                       const clang::FunctionDecl *FunctionDeclInfo,
                       ReturnTypeInfo &ReturnType,
                       const clang::QualType QualType);

bool processFuncReturnType(
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
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

        FuncInfo.ReturnType.InnerType = AlreadyExported->second.ExportedName;
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
      FuncInfo.ReturnType.Type = AlreadyExported->second.ExportedName;
    }
  } else if (ReturnType->isEnumeralType()) {

    // Check if ReturnType is a ElaboratedType, if it is, we need to get the
    // named type, which is the actual enum.
    if (auto *ElabType = ReturnType->getAs<clang::ElaboratedType>()) {
      ReturnType = ElabType->getNamedType();
    }

    // Check if it is already parsed
    auto FullyQualifiedName = ReturnType.getAsString();

    if (FullyQualifiedName.find("enum ") != std::string::npos) {
      FullyQualifiedName.erase(0, 5);
    }

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
    FuncInfo.ReturnType.Type = AlreadyExported->second.ExportedName;
    FuncInfo.ReturnType.IsEnum = true;
  } else if (ReturnType->isPointerType() || ReturnType->isReferenceType()) {
    auto PointeeType = ReturnType->getPointeeType();

    FuncInfo.ReturnType.IsReference = ReturnType->isReferenceType();

    if (!processPointerRet(ParsedClassesMap, D, FuncInfo.ReturnType,
                           PointeeType)) {
      return false;
    }
  }

  return true;
}
void processFunctionDecl(
    std::map<std::string, ExportedTypeInfo> &ParsedClassesMap,
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
  if (const auto *Parent = D->getParent();
      Parent && Parent->isRecord() && !D->isStatic()) {
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

    FuncInfo.ContainingClass = std::get<std::shared_ptr<ExportedClass>>(
        AlreadyExported->second.TypeData);
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
    } else if (Param->getType()->isPointerType() ||
               Param->getType()->isReferenceType()) {
      if (!processPointerParam(ParsedClassesMap, D, Param, FuncParam)) {
        return;
      }
    } else if (Param->getType()->isBuiltinType()) {
      FuncParam.Type = Param->getType().getAsString();
    } else if (Param->getType()->isEnumeralType()) {
      auto ParamType = Param->getType();

      // Check if ReturnType is a ElaboratedType, if it is, we need to get the
      // named type, which is the actual enum.
      if (auto *ElabType = ParamType->getAs<clang::ElaboratedType>()) {
        ParamType = ElabType->getNamedType();
      }

      auto FullyQualifiedName = ParamType.getAsString();

      if (FullyQualifiedName.find("enum ") != std::string::npos) {
        FullyQualifiedName.erase(0, 5);
      }

      auto AlreadyExported = ParsedClassesMap.find(FullyQualifiedName);
      if (AlreadyExported == ParsedClassesMap.end()) {
        unsigned DiagID = D->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

        clang::DiagnosticsEngine &DiagEngine =
            D->getASTContext().getDiagnostics();
        DiagEngine.Report(Param->getLocation(), DiagID) << FullyQualifiedName;
        return;
      }

      FuncParam.Type = AlreadyExported->second.ExportedName;
      FuncParam.RealType = AlreadyExported->second.Name;
      FuncParam.EnumType = FullyQualifiedName;
    }

    // Add the parameter
    FuncInfo.Parameters.push_back(FuncParam);
  }

  FunctionInfos.push_back(FuncInfo);
}

bool processPointerRet(std::map<std::string, ExportedTypeInfo> &ExportedTypes,
                       const clang::FunctionDecl *FunctionDeclInfo,
                       ReturnTypeInfo &ReturnType,
                       const clang::QualType PointeeType) {

  // Check if the pointee is a record
  if (PointeeType->isRecordType()) {
    clang::QualType RecordType = PointeeType;
    // Check if it as an elaborated type
    if (auto *ElabType = PointeeType->getAs<clang::ElaboratedType>()) {
      RecordType = ElabType->getNamedType();
    }

    auto FullyQualifiedName = RecordType.getAsString();

    // Check if it has struct or class and remove it
    if (FullyQualifiedName.find("struct ") != std::string::npos) {
      FullyQualifiedName.erase(0, 7);
    } else if (FullyQualifiedName.find("class ") != std::string::npos) {
      FullyQualifiedName.erase(0, 6);
    }

    // Okay, we have a class, check if it is already parsed
    auto AlreadyExported = ExportedTypes.find(FullyQualifiedName);
    if (AlreadyExported == ExportedTypes.end()) {
      unsigned DiagID =
          FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
              clang::DiagnosticsEngine::Error, "Error: %0 is not exported\n");

      clang::DiagnosticsEngine &DiagEngine =
          FunctionDeclInfo->getASTContext().getDiagnostics();
      DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
          << FullyQualifiedName;
      return false;
    }

    // We export it.
    ReturnType.Type = AlreadyExported->second.ExportedName + "*";
  } else if (PointeeType->isBuiltinType()) {
    // We don't need to do anything
    ReturnType.Type = PointeeType.getAsString() + "*";
  } else if (PointeeType->isEnumeralType()) {
    ReturnType.Type = PointeeType.getAsString() + "*";
  } else if (PointeeType->isPointerType()) {
    unsigned DiagID =
        FunctionDeclInfo->getASTContext().getDiagnostics().getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "Error: %0 is a pointer to a pointer, it is currently not "
            "supported");

    clang::DiagnosticsEngine &DiagEngine =
        FunctionDeclInfo->getASTContext().getDiagnostics();
    DiagEngine.Report(FunctionDeclInfo->getLocation(), DiagID)
        << PointeeType.getAsString();
    return false;
  }

  return true;
}