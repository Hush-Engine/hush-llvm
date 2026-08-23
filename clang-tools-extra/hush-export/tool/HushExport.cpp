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
    "-DHUSH_HEADER_PARSING=1",
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
  std::error_code HeaderEC;
  llvm::raw_fd_ostream HeaderOutFile("HushBindings.h", HeaderEC,
                                     llvm::sys::fs::OF_Text);
  if (HeaderEC) {
    llvm::errs() << "Error opening HushBindings.h: " << HeaderEC.message()
                 << "\n";
    return 1;
  }

  std::error_code CppEC;
  llvm::raw_fd_ostream CppOutFile("HushBindings.cpp", CppEC,
                                  llvm::sys::fs::OF_Text);
  if (CppEC) {
    llvm::errs() << "Error opening HushBindings.cpp: " << CppEC.message()
                 << "\n";
    return 1;
  }

  hush::CodeEmitter Emitter;
  Emitter.emit(Matcher.getIR(), HeaderOutFile, CppOutFile);

  HeaderOutFile.close();
  CppOutFile.close();
  if (HeaderOutFile.has_error() || CppOutFile.has_error()) {
    llvm::errs() << "Error writing generated binding files\n";
    HeaderOutFile.clear_error();
    CppOutFile.clear_error();
    return 1;
  }

  return Result;
}
