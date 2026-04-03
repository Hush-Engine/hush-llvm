#include "HushReflectionMatcher.h"

#include "ASTExtractor.h"
#include "ReflectionEmitter.h"

// using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

static llvm::StringRef
getRecordFilename(const clang::CXXRecordDecl *Record,
                  const clang::SourceManager &SM) {
  clang::SourceLocation Loc = Record->getLocation();
  if (Loc.isInvalid()) {
    return "";
  }
  return SM.getFilename(Loc);
}

HushReflectionCallback::HushReflectionCallback(llvm::StringRef CWD)
    : CurrentWorkingDirectory(CWD) {}

void HushReflectionCallback::run(const MatchFinder::MatchResult &Result) {
  const auto *Decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("id");

  const clang::SourceManager *SM = Result.SourceManager;
  StringRef Path = getRecordFilename(Decl, *SM);

  if (!Path.ends_with(".hpp"))
    return;

  if (!isFileInCurrentWorkingDirectory(Path))
    return;

  std::string PathStr = Path.str();

  if (hasFileBeenParsed(PathStr))
    return;

  // Layer 2: Extract model from AST
  hush_reflection::ClassModel Model =
      hush_reflection::extractClassModel(Decl);

  // Layer 3: Emit code from model
  std::string OutputPath = PathStr;
  size_t Pos = OutputPath.rfind(".hpp");
  OutputPath.insert(Pos, ".hushgen");

  std::error_code EC;
  llvm::raw_fd_ostream Out(OutputPath, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "Error opening file " << OutputPath << ": " << EC.message()
                 << "\n";
    return;
  }

  hush_reflection::ReflectionEmitter Emitter;
  Emitter.emit(Model, Out);

  ParsedFiles.insert(PathStr);
  ReflectedTypes.push_back({Model.QualifiedName, PathStr});
  llvm::outs() << "Generated reflection data in " << OutputPath << "\n";
}

bool HushReflectionCallback::hasFileBeenParsed(const std::string &Path) const {
  return ParsedFiles.find(Path) != ParsedFiles.end();
}

bool HushReflectionCallback::isFileInCurrentWorkingDirectory(
    StringRef Path) const {
  return Path.starts_with(CurrentWorkingDirectory);
}
