#pragma once

#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <set>
#include <string>
#include <vector>

struct ReflectedTypeEntry {
  /// The fully qualified name of the class, including namespaces.
  std::string QualifiedClassName;
  /// Absolute path to the header file where the class is defined.
  std::string HeaderPath;
};

class HushReflectionCallback
    : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  HushReflectionCallback(llvm::StringRef CWD);

  void run(const clang::ast_matchers::MatchFinder::MatchResult &Result)
      override;

  bool hasFileBeenParsed(const std::string &Path) const;

  bool isFileInCurrentWorkingDirectory(llvm::StringRef Path) const;

  const std::vector<ReflectedTypeEntry> &getReflectedTypes() const {
    return ReflectedTypes;
  }

private:
  std::vector<ReflectedTypeEntry> ReflectedTypes;
  std::set<std::string> ParsedFiles;
  llvm::StringRef CurrentWorkingDirectory;
};
