// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "HushReflectionMatcher.h"

#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ExtractAPI/API.h>

#include "llvm/DebugInfo/CodeView/CodeView.h"

using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

DeclarationMatcher HushReflectionMatcher =
    cxxRecordDecl(decl(hasAttr(clang::attr::HushReflect)).bind("id"));

int main(int argc, const char **argv) {
  auto StartTime = llvm::TimeRecord::getCurrentTime();

  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }

  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  // Add define to the compiler options

  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  Tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster("-DHUSH_HEADER_PARSING=1",
                                clang::tooling::ArgumentInsertPosition::BEGIN));

  // Get the current working directory

  SmallString<1024> CWDBuffer;
  llvm::sys::fs::current_path(CWDBuffer);

  StringRef CWD = CWDBuffer.str();

  HushReflectionCallback Callback(CWD);

  MatchFinder Finder;

  // Spelled in source matcher
  Finder.addMatcher(
      traverse(clang::TK_IgnoreUnlessSpelledInSource, HushReflectionMatcher),
      &Callback);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
  }

  auto EndTime = llvm::TimeRecord::getCurrentTime();

  auto Duration = EndTime.getUserTime() - StartTime.getUserTime();
        llvm::outs() << "Hush Reflection Tool completed in "
                 << Duration << " seconds.\n";

  return 0;
}