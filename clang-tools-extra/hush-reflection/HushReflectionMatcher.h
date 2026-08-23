#pragma once

#include "ReflectionModel.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <map>
#include <set>
#include <string>
#include <vector>

struct ReflectedTypeEntry {
  /// The fully qualified name of the class, including namespaces.
  std::string QualifiedClassName;
  /// Absolute path to the header file where the class is defined.
  std::string HeaderPath;
  /// Set when the class is marked with [[hush::system]].
  bool IsSystem = false;
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

  const std::vector<hush_reflection::ModuleInitFunction> &
  getModuleInitFunctions() const {
    return ModuleInitFunctions;
  }

  /// Set to true when any annotation configuration error was reported.
  bool hasErrors() const { return HasErrors; }

  /// Writes all per-header generated files after the complete tool run has
  /// succeeded, so a later parse error cannot truncate valid prior output.
  bool writeOutputs();

private:
  std::vector<ReflectedTypeEntry> ReflectedTypes;
  std::vector<hush_reflection::ModuleInitFunction> ModuleInitFunctions;
  std::set<std::string> ParsedModuleInitFunctions;
  /// Maps each parsed header to the qualified name of its reflected type.
  std::map<std::string, std::string> ParsedFiles;
  std::map<std::string, std::string> PendingOutputs;
  llvm::StringRef CurrentWorkingDirectory;
  bool HasErrors = false;
};
