#include "HushReflectionMatcher.h"

#include "ASTExtractor.h"
#include "ReflectionEmitter.h"
#include "llvm/Support/Path.h"

// using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

static llvm::StringRef
getDeclFilename(const clang::Decl *Decl, const clang::SourceManager &SM) {
  clang::SourceLocation Loc = Decl->getLocation();
  if (Loc.isInvalid()) {
    return "";
  }
  return SM.getFilename(Loc);
}

HushReflectionCallback::HushReflectionCallback(llvm::StringRef CWD)
    : CurrentWorkingDirectory(CWD) {}

void HushReflectionCallback::run(const MatchFinder::MatchResult &Result) {
  clang::DiagnosticsEngine &Diags = Result.Context->getDiagnostics();

  // Free functions marked with [[hush::module_init]] are collected so the
  // generated RegisterModule function can call them.
  if (const auto *Func =
          Result.Nodes.getNodeAs<clang::FunctionDecl>("module_init")) {
    StringRef Path = getDeclFilename(Func, *Result.SourceManager);
    if (!Path.ends_with(".hpp") || !isFileInCurrentWorkingDirectory(Path)) {
      return;
    }
    std::optional<hush_reflection::ModuleInitFunction> InitFn =
        hush_reflection::extractModuleInitFunction(Func, Diags);
    if (InitFn.has_value()) {
      InitFn->HeaderPath = Path.str();
      std::string Key = InitFn->HeaderPath + "\n" + InitFn->QualifiedName;
      if (ParsedModuleInitFunctions.insert(std::move(Key)).second)
        ModuleInitFunctions.push_back(*InitFn);
    } else {
      HasErrors = true;
    }
    return;
  }

  const auto *Decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("id");
  if (Decl == nullptr) {
    return;
  }

  Decl = Decl->getDefinition();
  if (Decl == nullptr) {
    return;
  }

  const clang::SourceManager *SM = Result.SourceManager;
  StringRef Path = getDeclFilename(Decl, *SM);

  if (!Path.ends_with(".hpp"))
    return;

  if (!isFileInCurrentWorkingDirectory(Path))
    return;

  std::string PathStr = Path.str();

  // The same header is parsed once per translation unit of the target, so a
  // file is only emitted once. A different reflected class in an already
  // parsed file is an error: the generated .hushgen.hpp redefines
  // HUSH_GENERATED_BODY once per header, so only one reflected type can
  // live in a single header.
  const auto ParsedIt = ParsedFiles.find(PathStr);
  if (ParsedIt != ParsedFiles.end()) {
    if (ParsedIt->second != Decl->getQualifiedNameAsString()) {
      unsigned Id = Diags.getDiagnosticIDs()->getCustomDiagID(
          clang::DiagnosticIDs::Error,
          "only one hush reflected type is allowed per header file");
      Diags.Report(Decl->getLocation(), Id);
      HasErrors = true;
    }
    return;
  }

  // Layer 2: Extract model from AST
  hush_reflection::ClassModel Model =
      hush_reflection::extractClassModel(Decl, Diags);

  // Layer 3: Emit code from model
  std::string OutputPath = PathStr;
  size_t Pos = OutputPath.rfind(".hpp");
  OutputPath.insert(Pos, ".hushgen");

  std::string Generated;
  llvm::raw_string_ostream Out(Generated);
  hush_reflection::ReflectionEmitter Emitter;
  Emitter.emit(Model, Out);
  Out.flush();

  ParsedFiles.emplace(PathStr, Model.QualifiedName);
  PendingOutputs.emplace(OutputPath, std::move(Generated));
  ReflectedTypes.push_back(
      {Model.QualifiedName, PathStr, Model.IsSystem});
}

bool HushReflectionCallback::writeOutputs() {
  for (const auto &[OutputPath, Generated] : PendingOutputs) {
    std::error_code EC;
    llvm::raw_fd_ostream Out(OutputPath, EC, llvm::sys::fs::OF_Text);
    if (EC) {
      llvm::errs() << "Error opening file " << OutputPath << ": "
                   << EC.message() << "\n";
      return false;
    }
    Out << Generated;
    Out.close();
    if (Out.has_error()) {
      llvm::errs() << "Error writing file " << OutputPath << "\n";
      Out.clear_error();
      return false;
    }
    llvm::outs() << "Generated reflection data in " << OutputPath << "\n";
  }
  return true;
}

bool HushReflectionCallback::hasFileBeenParsed(const std::string &Path) const {
  return ParsedFiles.find(Path) != ParsedFiles.end();
}

bool HushReflectionCallback::isFileInCurrentWorkingDirectory(
    StringRef Path) const {
  if (!Path.starts_with(CurrentWorkingDirectory))
    return false;
  if (Path.size() == CurrentWorkingDirectory.size())
    return true;
  return llvm::sys::path::is_separator(Path[CurrentWorkingDirectory.size()]);
}
