// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "HushReflectionMatcher.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"

#include <algorithm>

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

// Create a custom option that is a string and called "output stamp"
static llvm::cl::opt<std::string> OutputStamp(
    "output-stamp", llvm::cl::desc("Output stamp for the reflection tool"),
    llvm::cl::init("hush-reflection"), llvm::cl::cat(MyToolCategory));

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


 // This is needed for msvc compatibility, I commented it out until I have a proper way to pass this as an arg to the executable from Hush's build system
 //
 //  CommandLineArguments AdditionalArgs {
 //    #ifdef _WIN64
 //    "-D_WIN64"
 //    #endif
 //  };


	// AdditionalArgs.emplace_back("-target");
	// AdditionalArgs.emplace_back("x86_64-pc-windows-msvc");
	// AdditionalArgs.emplace_back("-fms-compatibility");
	// AdditionalArgs.emplace_back("-fms-extensions");

 //  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
 //    AdditionalArgs,
 //    ArgumentInsertPosition::BEGIN
 //  ));

  Tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster("-DHUSH_HEADER_PARSING=1",
                                clang::tooling::ArgumentInsertPosition::BEGIN));

  // Ignore all warnings
  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      "-w", clang::tooling::ArgumentInsertPosition::BEGIN));

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
    return Result;
  }

  std::string StampPath = OutputStamp.getValue();

  std::error_code EC;
  llvm::raw_fd_ostream OutFile(StampPath, EC, llvm::sys::fs::OF_Text);
  llvm::outs() << "Generating output stamp file: " << StampPath << "\n";
  if (EC) {
    llvm::errs() << "Error opening file " << StampPath << ": " << EC.message()
                 << "\n";
    return 1;
  }
  // Derive function name from stamp path:
  // "C:/.../HushCore.hushgen.cpp" -> "HushCore.hushgen" -> "HushCore"
  StringRef StampStem = llvm::sys::path::stem(StampPath);
  StringRef TargetName = llvm::sys::path::stem(StampStem);

  std::string FunctionName = "RegisterReflectedTypes_";
  FunctionName += TargetName.str();

  OutFile << "// AUTO-GENERATED -- DO NOT EDIT\n";

  for (const auto &Entry : Callback.getReflectedTypes()) {
    StringRef RelPath(Entry.HeaderPath);
    RelPath.consume_front(CWD);
    if (RelPath.starts_with("/") || RelPath.starts_with("\\"))
      RelPath = RelPath.drop_front(1);

    // Normalize backslashes to forward slashes for #include
    std::string Normalized = RelPath.str();
    std::replace(Normalized.begin(), Normalized.end(), '\\', '/');

    OutFile << "#include \"" << Normalized << "\"\n";
  }

  OutFile << "\n#include <reflection/Type.hpp>\n\n";

  OutFile << "void " << FunctionName
          << "([[maybe_unused]] Hush::Reflection::ReflectionDB &db) {\n";

  if (!Callback.getReflectedTypes().empty()) {
    for (const auto &Entry : Callback.getReflectedTypes()) {
      OutFile << "    " << Entry.QualifiedClassName
              << "::RegisterReflection(db);\n";
    }
  }

  OutFile << "}\n";
  OutFile.close();

  auto EndTime = llvm::TimeRecord::getCurrentTime();

  auto Duration = EndTime.getUserTime() - StartTime.getUserTime();
  llvm::outs() << "Hush Reflection Tool completed in " << Duration
               << " seconds.\n";

  return 0;
}
