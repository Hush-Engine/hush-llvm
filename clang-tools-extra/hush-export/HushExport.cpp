// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ExtractAPI/API.h>

#include "DefParser.h"

using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

DeclarationMatcher HushExportAttrMatcher =
    decl(recordDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

DeclarationMatcher HushExportFunctionMatcher =
    decl(functionDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

// DeclarationMatcher HushExportAttrMatcher =
//     decl(anyOf(functionDecl(attr()
//                recordDecl(hasAttr(clang::attr::HushExport))))
//         .bind("hushExportable");

class HushBindingMatcher : public MatchFinder::MatchCallback {
  std::map<std::string, ExportedClassInfo> &ParsedClasses;
  llvm::raw_fd_ostream &HeaderOutFile;
  llvm::raw_fd_ostream &CppOutFile;
  llvm::raw_fd_ostream &PrototypeOutFile;

public:
  HushBindingMatcher(std::map<std::string, ExportedClassInfo> &ParsedClasses,
                     llvm::raw_fd_ostream &HeaderOutFile,
                     llvm::raw_fd_ostream &CppOutFile,
                     llvm::raw_fd_ostream &PrototypeOutFile)
      : ParsedClasses(ParsedClasses), HeaderOutFile(HeaderOutFile),
        CppOutFile(CppOutFile), PrototypeOutFile(PrototypeOutFile) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    if (const clang::RecordDecl *D =
            Result.Nodes.getNodeAs<clang::RecordDecl>("hushExportable")) {
      clang::HushExportAttr *HushExportAttr =
          D->getAttr<clang::HushExportAttr>();
      processClassDecl(ParsedClasses, HushExportAttr, D, HeaderOutFile,
                       CppOutFile);
    }

    // Get HushExport attribute, it should be one child
    if (const clang::FunctionDecl *FD =
            Result.Nodes.getNodeAs<clang::FunctionDecl>("hushExportable")) {
      llvm::outs() << "Function: " << FD->getQualifiedNameAsString() << "\n";
    }
  }
};

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  std::error_code EC;
  llvm::raw_fd_ostream HeaderOutFile("HushBindings.h", EC,
                                     llvm::sys::fs::OF_Text);
  llvm::raw_fd_ostream CppOutFile("HushBindings.cpp", EC,
                                  llvm::sys::fs::OF_Text);
  llvm::raw_fd_ostream PrototypeOutFile("HushBindings.inc", EC,
                                        llvm::sys::fs::OF_Text);

  HeaderOutFile << "#pragma once\n"
                   "#include <stdint.h>\n\n\n"
                   "#ifdef __cplusplus\n"
                   "extern \"C\" {\n"
                   "#endif\n\n";

  CppOutFile << "#include \"HushBindings.h\"\n\n";

  std::map<std::string, ExportedClassInfo> ParsedClasses;
  HushBindingMatcher Printer(ParsedClasses, HeaderOutFile, CppOutFile,
                             PrototypeOutFile);

  MatchFinder Finder;
  Finder.addMatcher(HushExportAttrMatcher, &Printer);
  Finder.addMatcher(HushExportFunctionMatcher, &Printer);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
    return Result;
  }

  // Finally, write the contents of the Prototype file to the header file
  PrototypeOutFile.seek(0);

  PrototypeOutFile.close();

  // Open the file and write it to the header file
  llvm::raw_fd_stream PrototypeOutFileRead("HushBindings.inc", EC);
  ssize_t ReadBytes;
  char Buffer[4096];

  while ((ReadBytes = PrototypeOutFileRead.read(Buffer, sizeof(Buffer))) > 0) {
    HeaderOutFile.write(Buffer, ReadBytes);
  }

  HeaderOutFile << "\n";
  HeaderOutFile << "#ifdef __cplusplus\n"
                   "}\n"
                   "#endif\n";

  HeaderOutFile.close();
  CppOutFile.close();

  // Delete the temporary file
  llvm::sys::fs::remove("HushBindings.inc");

  return Result;
}
