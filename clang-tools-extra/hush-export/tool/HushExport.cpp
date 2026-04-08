// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>

#include "CodeEmitter.h"
#include "ExportMatcher.h"

using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("hush-export options");

DeclarationMatcher HushExportAttrMatcher =
    decl(recordDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

DeclarationMatcher HushExportFunctionMatcher =
    decl(functionDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

DeclarationMatcher HushExportEnumMatcher =
    decl(enumDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

static cl::opt<std::string> CompilerTarget(
  "compiler-target",
  cl::desc("The compiler's compatibility mode: 'clang' or 'msvc' (defaults to clang)"),
  cl::value_desc("mode"),
  cl::init("clang"),
  cl::cat(MyToolCategory)
);

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  CompilationDatabase& CompileDb = OptionsParser.getCompilations();
  const auto& PathList = OptionsParser.getSourcePathList();

  ClangTool Tool(CompileDb, PathList);
  CommandLineArguments AdditionalArgs {
    #ifdef _WIN64
    "-D_WIN64"
    #endif
  };

  if (CompilerTarget == "msvc") {
    AdditionalArgs.emplace_back("-target");
    AdditionalArgs.emplace_back("x86_64-pc-windows-msvc");
    AdditionalArgs.emplace_back("-fms-compatibility");
    AdditionalArgs.emplace_back("-fms-extensions");
  }

  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
    AdditionalArgs,
    ArgumentInsertPosition::BEGIN
  ));

  std::error_code EC;
  llvm::raw_fd_ostream HeaderOutFile("HushBindings.h", EC,
                                     llvm::sys::fs::OF_Text);
  llvm::raw_fd_ostream CppOutFile("HushBindings.cpp", EC,
                                  llvm::sys::fs::OF_Text);

  // Phase 1: AST extraction → CBindingIR (single pass)
  hush::ExportMatcher Matcher;

  MatchFinder Finder;
  Finder.addMatcher(HushExportEnumMatcher, &Matcher);
  Finder.addMatcher(HushExportAttrMatcher, &Matcher);
  Finder.addMatcher(HushExportFunctionMatcher, &Matcher);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
    return Result;
  }

  // Phase 2: Emit code
  hush::CodeEmitter Emitter;
  Emitter.emit(Matcher.getIR(), HeaderOutFile, CppOutFile);

  return Result;
}
