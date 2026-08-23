// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "HushReflectionMatcher.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <cctype>
#include <set>

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

// Reflected records are classes marked with any of the hush type attributes.
DeclarationMatcher HushReflectionMatcher =
    cxxRecordDecl(isDefinition(),
                  anyOf(hasAttr(clang::attr::HushReflect),
                        hasAttr(clang::attr::HushSystem),
                        hasAttr(clang::attr::HushComponent),
                        hasAttr(clang::attr::HushBuiltin)))
        .bind("id");

// Module init functions are free functions marked with [[hush::module_init]].
DeclarationMatcher HushModuleInitMatcher =
    functionDecl(hasAttr(clang::attr::HushModuleInit), unless(cxxMethodDecl()))
        .bind("module_init");

// Create a custom option that is a string and called "output stamp"
static llvm::cl::opt<std::string> OutputStamp(
    "output-stamp", llvm::cl::desc("Output stamp for the reflection tool"),
    llvm::cl::init("hush-reflection"), llvm::cl::cat(MyToolCategory));

// Name of the module being generated, used for the RegisterModule functions.
// When empty it is derived from the output stamp path.
static llvm::cl::opt<std::string> ModuleName(
    "module-name", llvm::cl::desc("Name of the generated module"),
    llvm::cl::init(""), llvm::cl::cat(MyToolCategory));

// Path of the generated RegisterModule header. When empty it is written next
// to the output stamp.
static llvm::cl::opt<std::string> ModuleHeader(
    "module-header",
    llvm::cl::desc("Output path of the generated RegisterModule.hpp"),
    llvm::cl::init(""), llvm::cl::cat(MyToolCategory));

// When set, the generated source also exposes the extern "C"
// HushRegisterModule entry point used by dynamically loaded modules.
static llvm::cl::opt<bool> EmitModuleEntry(
    "emit-module-entry",
    llvm::cl::desc("Emit the extern \"C\" HushRegisterModule entry point"),
    llvm::cl::init(false), llvm::cl::cat(MyToolCategory));

namespace {

// Makes a path relative to the current working directory and normalizes the
// slashes so it can be used in an #include directive.
std::string makeRelativeInclude(llvm::StringRef Path, llvm::StringRef CWD) {
  StringRef Rel(Path);
  Rel.consume_front(CWD);
  if (Rel.starts_with("/") || Rel.starts_with("\\"))
    Rel = Rel.drop_front(1);

  std::string Normalized = Rel.str();
  std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
  return Normalized;
}

bool isValidModuleName(llvm::StringRef Name) {
  if (Name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(Name.front())) ||
        Name.front() == '_'))
    return false;
  return std::all_of(Name.drop_front().begin(), Name.drop_front().end(),
                     [](char C) {
                       return std::isalnum(static_cast<unsigned char>(C)) ||
                              C == '_';
                     });
}

} // namespace

int main(int argc, const char **argv) {
  auto StartTime = llvm::TimeRecord::getCurrentTime();

  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }

  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

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
  Finder.addMatcher(
      traverse(clang::TK_IgnoreUnlessSpelledInSource, HushModuleInitMatcher),
      &Callback);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
    return Result;
  }

  if (Callback.hasErrors()) {
    llvm::errs() << "Invalid hush annotation configuration found\n";
    return 1;
  }

  std::string StampPath = OutputStamp.getValue();

  // Derive the module name from the stamp path when not given:
  // "C:/.../HushCore.hushgen.cpp" -> "HushCore.hushgen" -> "HushCore"
  std::string Module = ModuleName.getValue();
  if (Module.empty()) {
    StringRef StampStem = llvm::sys::path::stem(StampPath);
    Module = llvm::sys::path::stem(StampStem).str();
  }
  if (!isValidModuleName(Module)) {
    llvm::errs() << "Invalid module name '" << Module
                 << "': expected a C++ identifier\n";
    return 1;
  }

  // The module header defaults to the same Module/RegisterModule.hpp layout
  // used by the include emitted into the generated source.
  std::string HeaderPath = ModuleHeader.getValue();
  if (HeaderPath.empty()) {
    HeaderPath = llvm::sys::path::parent_path(StampPath).str();
    if (!HeaderPath.empty()) {
      HeaderPath += "/";
    }
    HeaderPath += Module + "/RegisterModule.hpp";
  }
  if (llvm::sys::path::filename(HeaderPath) != "RegisterModule.hpp" ||
      llvm::sys::path::filename(llvm::sys::path::parent_path(HeaderPath)) !=
          Module) {
    llvm::errs() << "Invalid module header path '" << HeaderPath
                 << "': expected <directory>/" << Module
                 << "/RegisterModule.hpp\n";
    return 1;
  }

  SmallString<256> HeaderDirectory(llvm::sys::path::parent_path(HeaderPath));
  if (!HeaderDirectory.empty()) {
    std::error_code DirectoryError =
        llvm::sys::fs::create_directories(HeaderDirectory);
    if (DirectoryError) {
      llvm::errs() << "Error creating directory " << HeaderDirectory << ": "
                   << DirectoryError.message() << "\n";
      return 1;
    }
  }

  if (!Callback.writeOutputs())
    return 1;

  // -------------------------------------------------------------------------
  // RegisterModule.hpp
  // -------------------------------------------------------------------------
  {
    std::error_code EC;
    llvm::raw_fd_ostream HeaderFile(HeaderPath, EC, llvm::sys::fs::OF_Text);
    if (EC) {
      llvm::errs() << "Error opening file " << HeaderPath << ": "
                   << EC.message() << "\n";
      return 1;
    }

    HeaderFile << "// AUTO-GENERATED -- DO NOT EDIT\n"
                  "#pragma once\n\n"
                  "#include <reflection/ModuleHandle.hpp>\n\n"
                  "#include <span>\n\n"
                  "namespace Hush\n"
                  "{\n"
                  "\tstruct SystemDescriptor;\n"
                  "}\n\n"
                  "namespace Hush::Reflection\n"
                  "{\n"
                  "\tclass ReflectionDB;\n"
                  "}\n\n"
                  "/// Registers every reflected type of the "
               << Module
               << " module into the reflection database.\n"
                  "/// The module handle says which module owns the types.\n"
                  "/// Returns true when every type was registered.\n"
                  "bool RegisterModule_"
               << Module
               << "(Hush::Reflection::ReflectionDB &db, Hush::ModuleHandle "
                  "module = Hush::ENGINE_MODULE_HANDLE);\n\n"
                  "/// Gets the descriptors of every system defined by the "
               << Module
               << " module.\n"
                  "std::span<const Hush::SystemDescriptor> GetSystemDescriptors_"
               << Module << "();\n";
    HeaderFile.close();
    if (HeaderFile.has_error()) {
      llvm::errs() << "Error writing file " << HeaderPath << "\n";
      HeaderFile.clear_error();
      return 1;
    }
  }

  // -------------------------------------------------------------------------
  // RegisterModule source (the output stamp file)
  // -------------------------------------------------------------------------
  llvm::outs() << "Generating output stamp file: " << StampPath << "\n";

  std::error_code EC;
  llvm::raw_fd_ostream OutFile(StampPath, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "Error opening file " << StampPath << ": " << EC.message()
                 << "\n";
    return 1;
  }

  OutFile << "// AUTO-GENERATED -- DO NOT EDIT\n";
  OutFile << "#include \"" << Module << "/RegisterModule.hpp\"\n";

  // A header can hold both a reflected type and a module init function, so
  // includes are deduplicated.
  std::set<std::string> EmittedIncludes;
  for (const auto &Entry : Callback.getReflectedTypes()) {
    std::string Include = makeRelativeInclude(Entry.HeaderPath, CWD);
    if (EmittedIncludes.insert(Include).second) {
      OutFile << "#include \"" << Include << "\"\n";
    }
  }
  for (const auto &InitFn : Callback.getModuleInitFunctions()) {
    std::string Include = makeRelativeInclude(InitFn.HeaderPath, CWD);
    if (EmittedIncludes.insert(Include).second) {
      OutFile << "#include \"" << Include << "\"\n";
    }
  }

  OutFile << "\n#include <reflection/Type.hpp>\n";
  OutFile << "#include <SystemDescriptor.hpp>\n\n";

  OutFile << "bool RegisterModule_" << Module
          << "(Hush::Reflection::ReflectionDB &db, Hush::ModuleHandle module) "
             "{\n";

  if (!Callback.getReflectedTypes().empty()) {
    OutFile << "    bool ok = true;\n";
    for (const auto &Entry : Callback.getReflectedTypes()) {
      OutFile << "    ok = " << Entry.QualifiedClassName
              << "::RegisterReflection(db, module) && ok;\n";
    }
    OutFile << "    if (!ok) { return false; }\n";
  }

  for (const auto &InitFn : Callback.getModuleInitFunctions()) {
    OutFile << "    " << InitFn.QualifiedName << "();\n";
  }

  if (!Callback.getReflectedTypes().empty()) {
    OutFile << "    return true;\n";
  } else {
    OutFile << "    (void)db;\n    (void)module;\n    return true;\n";
  }
  OutFile << "}\n\n";

  // System descriptors of this module.
  OutFile << "std::span<const Hush::SystemDescriptor> GetSystemDescriptors_"
          << Module << "() {\n";
  bool HasSystems = false;
  for (const auto &Entry : Callback.getReflectedTypes()) {
    if (Entry.IsSystem) {
      HasSystems = true;
      break;
    }
  }
  if (HasSystems) {
    OutFile << "    static const Hush::SystemDescriptor systems[] = {\n";
    for (const auto &Entry : Callback.getReflectedTypes()) {
      if (!Entry.IsSystem) {
        continue;
      }
      OutFile << "        " << Entry.QualifiedClassName
              << "::GetSystemDescriptor(),\n";
    }
    OutFile << "    };\n    return systems;\n";
  } else {
    OutFile << "    return {};\n";
  }
  OutFile << "}\n";

  // Optional extern "C" entry point for dynamically loaded modules.
  if (EmitModuleEntry) {
    OutFile << "\n#include <HushModuleAbi.h>\n"
               "#include <NativeModuleEntry.hpp>\n\n"
               "extern \"C\" HUSH_MODULE_EXPORT HushModuleResult HushRegisterModule(\n"
               "    const HushModuleContext *context) {\n"
               "    return Hush::Modules::NativeModuleEntry(context, "
               "&RegisterModule_"
            << Module << ", GetSystemDescriptors_" << Module << "());\n"
            << "}\n";
  }

  OutFile.close();
  if (OutFile.has_error()) {
    llvm::errs() << "Error writing file " << StampPath << "\n";
    OutFile.clear_error();
    return 1;
  }

  auto EndTime = llvm::TimeRecord::getCurrentTime();

  auto Duration = EndTime.getUserTime() - StartTime.getUserTime();
  llvm::outs() << "Hush Reflection Tool completed in " << Duration
               << " seconds.\n";

  return 0;
}
