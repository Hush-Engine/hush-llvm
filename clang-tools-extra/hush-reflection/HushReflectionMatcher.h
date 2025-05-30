#pragma once
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ExtractAPI/API.h>

#include "llvm/DebugInfo/CodeView/CodeView.h"

using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

class HushReflectionCallback : public MatchFinder::MatchCallback {
public:
  HushReflectionCallback(llvm::StringRef CWD);

  void run(const MatchFinder::MatchResult &Result) override;

  bool hasFileBeenParsed(const std::string &Path) const;

  bool isFileInCurrentWorkingDirectory(StringRef Path) const;

private:
  std::set<std::string> ParsedFiles;
  StringRef CurrentWorkingDirectory;
};
