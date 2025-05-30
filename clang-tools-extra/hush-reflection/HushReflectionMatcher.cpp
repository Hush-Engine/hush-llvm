#include "HushReflectionMatcher.h"

#include "ReflectedClass.h"

StringRef getRecordFilename(const clang::CXXRecordDecl *Record,
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
  const clang::CXXRecordDecl *HushReflectionClass =
      Result.Nodes.getNodeAs<clang::CXXRecordDecl>("id");

  // Get header
  const clang::SourceManager *SM = Result.SourceManager;

  // Get the path of Dir
  StringRef Path = getRecordFilename(HushReflectionClass, *SM);

  if (!Path.ends_with(".hpp")) {
    return;
  }

  if (!isFileInCurrentWorkingDirectory(Path)) {
    return;
  }

  std::string PathStr = Path.str();

  if (hasFileBeenParsed(PathStr)) {
    return;
  }

  // For the path, replace the ".hpp" with ".hushgen.hpp"
  std::string OutputPath = PathStr;
  size_t Pos = OutputPath.rfind(".hpp");
  OutputPath.insert(Pos, ".hushgen");

  // Write the reflection data to the file
  std::error_code EC;
  llvm::raw_fd_ostream Out(OutputPath, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "Error opening file " << OutputPath << ": " << EC.message()
                 << "\n";
    return;
  }

  ReflectedClass ReflectedClass(HushReflectionClass);

  ReflectedClass.generateReflectionCode(Out);
  ReflectedClass.generateSerializeCode(Out);
  ReflectedClass.generateDeserializeCode(Out);

  Out << "private:\n";

  ParsedFiles.insert(PathStr);
  llvm::outs() << "Generated reflection data in " << OutputPath << "\n";
}

bool HushReflectionCallback::hasFileBeenParsed(const std::string &Path) const {
  return ParsedFiles.find(Path) != ParsedFiles.end();
}

bool HushReflectionCallback::isFileInCurrentWorkingDirectory(
    StringRef Path) const {
  return Path.starts_with(CurrentWorkingDirectory);
}