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
#include "FunctionParser.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BranchProbability.h"

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

struct FunctionPointerInfo {
  std::string Name;
  std::string ExportedName;
  std::string ReturnType;
  std::vector<std::string> Parameters;
};

class HushBindingMatcher : public MatchFinder::MatchCallback {
  std::map<std::string, std::shared_ptr<ExportedClass>> ParsedClasses;
  std::vector<std::shared_ptr<ExportedClass>> ParsedClassesVector;
  std::vector<FunctionInfo> Functions;

public:
  HushBindingMatcher() {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    if (const clang::RecordDecl *D =
            Result.Nodes.getNodeAs<clang::RecordDecl>("hushExportable")) {
      clang::HushExportAttr *HushExportAttr =
          D->getAttr<clang::HushExportAttr>();
      processClassDecl(ParsedClassesVector, ParsedClasses, HushExportAttr, D);
    }

    // Get HushExport attribute, it should be one child
    if (const clang::FunctionDecl *FD =
            Result.Nodes.getNodeAs<clang::FunctionDecl>("hushExportable")) {
      clang::HushExportAttr *HushExportAttr =
          FD->getAttr<clang::HushExportAttr>();
      processFunctionDecl(ParsedClasses, Functions, HushExportAttr, FD);
    }
  }

  std::map<std::string, std::shared_ptr<ExportedClass>> &getParsedClasses() {
    return ParsedClasses;
  }

  std::vector<std::shared_ptr<ExportedClass>> &getParsedClassesVector() {
    return ParsedClassesVector;
  }

  std::vector<FunctionInfo> &getFunctions() { return Functions; }
};

static void processClassHeader(
    std::string &HeaderFile,
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClassesVector);

static std::vector<FunctionPointerInfo>
processFunctions(std::string &HeaderFile, std::string &CppFile,
                 std::vector<FunctionInfo> &FunctionInfo);

static void
createFuncPointerTable(std::string &Header, std::string &CppFile,
                       const std::vector<FunctionPointerInfo> &Functions);

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

  std::string HeaderFile = "#pragma once\n"
                           "#include <stdint.h>\n\n\n"
                           "#ifdef __cplusplus\n"
                           "extern \"C\" {\n"
                           "#endif\n\n";

  std::string CppFile = "// Auto-generated file\n"
                        "// DO NOT EDIT\n\n"
                        "#include \"HushBindings.h\"\n\n";

  HushBindingMatcher Printer;

  MatchFinder Finder;
  Finder.addMatcher(HushExportAttrMatcher, &Printer);
  Finder.addMatcher(HushExportFunctionMatcher, &Printer);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
    return Result;
  }

  auto &ParsedClassesVector = Printer.getParsedClassesVector();

  // We need to process each one of the classes
  processClassHeader(HeaderFile, ParsedClassesVector);

  auto &Functions = Printer.getFunctions();
  auto FuncPointerInfo = processFunctions(HeaderFile, CppFile, Functions);

  createFuncPointerTable(HeaderFile, CppFile, FuncPointerInfo);

  HeaderFile += "\n#ifdef __cplusplus\n"
                "}\n"
                "#endif\n";

  // Write the header file
  HeaderOutFile << HeaderFile;
  CppOutFile << CppFile;

  return Result;
}

static void processClassHeader(
    std::string &HeaderFile,
    std::vector<std::shared_ptr<ExportedClass>> &ParsedClassesVector) {
  for (auto &ParsedClass : ParsedClassesVector) {
    if (ParsedClass->IsHandle) {
      HeaderFile += "typedef struct " + ParsedClass->ExportedName + ";\n\n";
    } else {
      // Process the class
      HeaderFile += "typedef struct " + ParsedClass->ExportedName + " {\n";
      for (auto &Member : ParsedClass->Members) {
        if (Member.IsPointer) {
          HeaderFile += "  " + Member.Type + " *" + Member.Name + ";\n";
        } else {
          HeaderFile += "  " + Member.Type + " " + Member.Name + ";\n";
        }
      }
      HeaderFile += "} " + ParsedClass->ExportedName + ";\n\n";
    }
  }
}

static std::vector<FunctionPointerInfo>
processFunctions(std::string &HeaderFile, std::string &CppFile,
                 std::vector<FunctionInfo> &FunctionInfo) {

  std::vector<FunctionPointerInfo> FunctionPointers;

  for (auto &Function : FunctionInfo) {
    std::string FunctionPrototype;

    std::string CallString;
    std::string ArgPreprocess;

    auto FuncPointerInfo = FunctionPointerInfo{};

    FuncPointerInfo.Name = Function.Name;
    FuncPointerInfo.ExportedName = Function.ExportedName;

    bool HasCallback = false;

    // For the return type, check if it is a special type, if it is, we need to
    // perform some special handling as we cannot return those types directly,
    // instead, for instance, for std::span<int> MyFunction(), the wrapper looks
    // like: void MyFunction(int* retData, size_t* retSize);, however, the
    // memory should be freed by the caller.

    if (Function.ReturnType.InnerType.size() > 0) {
      // Check if the type is std::span, std::string_view, std::string, or
      // std::vector For all of them, generate a function callback that receives
      // a pointer to the data and the size of the data.
      // For instance, given the function std::span<int> MyFunction(), the
      // generated function will look like: void MyFunction(int(*retFunc)(int*,
      // size_t, void*), void* retUserData);

      if (Function.ReturnType.Type.find("std::span") != std::string::npos ||
          Function.ReturnType.Type.find("std::string_view") !=
              std::string::npos ||
          Function.ReturnType.Type.find("std::string") != std::string::npos ||
          Function.ReturnType.Type.find("std::vector") != std::string::npos) {

        FunctionPrototype += "void " + Function.ExportedName + "(";
        FuncPointerInfo.ReturnType = "void";

        std::string InnerType = Function.ReturnType.InnerType;

        // If the type is std::uint* or std::int*, we need to remove the std::
        // part
        if (Function.ReturnType.Type.find("std::uint") != std::string::npos ||
            Function.ReturnType.Type.find("std::int") != std::string::npos) {
          InnerType.erase(0, 5);
        }

        // Check if the innertype contains const
        bool IsConst = InnerType.find("const") != std::string::npos;
        if (IsConst) {
          InnerType.erase(0, 6);
        }

        std::string ConstDef = IsConst ? "const " : "";

        FunctionPrototype += "void (*retFunc)(const " + InnerType + "* " +
                             ConstDef + ", size_t, void*), void* retUserData";

        FuncPointerInfo.Parameters.push_back("void (*retFunc)(const " +
                                             InnerType + "*, size_t, void*)");
        FuncPointerInfo.Parameters.push_back("void* retUserData");

        if (Function.Parameters.size() > 0) {
          FunctionPrototype += ", ";
        }

        HasCallback = true;
      }
    } else {

      bool IsConst =
          Function.ReturnType.Type.find("const") != std::string::npos;

      if (IsConst) {
        Function.ReturnType.Type.erase(0, 6);
      }

      std::string ReturnType = IsConst ? "const " : "";
      ReturnType += Function.ReturnType.Type;

      FuncPointerInfo.ReturnType = ReturnType;

      FunctionPrototype += ReturnType + " " + Function.ExportedName + "(";
    }

    if (Function.ContainingClass.has_value()) {
      FunctionPrototype +=
          Function.ContainingClass.value()->ExportedName + " *self";

      FuncPointerInfo.Parameters.push_back(
          Function.ContainingClass.value()->ExportedName + " *self");

      if (Function.Parameters.size() > 0) {
        FunctionPrototype += ", ";
      }

      // We need to replace the function name to the member function name,
      // for instace, for member function Hush::MyClass::MyFunction, we need to
      // remove the Hush::MyClass:: part. Luckily, the ContainingClass name is
      // that part.
      std::string FunctionName = Function.Name;
      FunctionName.replace(0, Function.ContainingClass.value()->Name.size(),
                           "");
      // Remove any :: that might be left
      FunctionName.erase(0, 2);

      ArgPreprocess += "\tauto selfClass = reinterpret_cast<" +
                       Function.ContainingClass.value()->Name + "*>(self);\n";

      CallString += "\tauto result______ = selfClass->" + FunctionName + "(";
    } else {
      CallString += "\tauto result______ = " + Function.Name + "(";
    }

    for (size_t I = 0; I < Function.Parameters.size(); ++I) {
      auto &Param = Function.Parameters[I];

      if (Param.InnerType.Type.size() > 0) {
        // Check if the type is std::span
        if (Param.Type.find("std::span") != std::string::npos ||
            Param.Type.find("std::string_view") != std::string::npos) {
          ArgPreprocess += "\tauto " + Param.Name + "Data__ = " + Param.Type +
                           "(" + Param.Name + "Data, " + Param.Name +
                           "Size);\n";

          std::string ConstDef = Param.InnerType.IsConst ? " const " : "";
          std::string PointerDef = Param.InnerType.IsPointer ? " *" : "";

          std::string InnerType = Param.InnerType.Type;

          if (Param.Type.find("std::uint") != std::string::npos ||
              Param.Type.find("std::int") != std::string::npos) {
            InnerType.erase(0, 5);
          }

          FunctionPrototype += InnerType + " *" + ConstDef + PointerDef +
                               Param.Name + "Data, const size_t " + Param.Name +
                               "Size";
          if (Param.IsConst) {
            FunctionPrototype = "const " + FunctionPrototype;
            FuncPointerInfo.Parameters.push_back("const " + InnerType + "* " +
                                                 ConstDef);
          } else {
            FuncPointerInfo.Parameters.push_back(InnerType + "* " + ConstDef);
          }

          FuncPointerInfo.Parameters.push_back("const size_t " + Param.Name +
                                               "Size");

          CallString += Param.Name + "Data__";
        }
      } else {
        std::string ConstDef = Param.IsConst ? "const " : "";
        std::string PointerDef =
            Param.IsPointer || Param.IsReference ? " *" : "";

        std::string ParamType = Param.Type;

        if (Param.Type.find("std::uint") != std::string::npos ||
            Param.Type.find("std::int") != std::string::npos) {
          ParamType.erase(0, 5);
        }

        ParamType = ConstDef + ParamType + PointerDef;

        FuncPointerInfo.Parameters.push_back(ParamType);

        FunctionPrototype += ParamType + " " + Param.Name;

        if (Param.IsReference) {
          // We need to cast the pointer to the reference
          CallString += "*";
        }
        CallString += Param.Name;
      }

      if (I != Function.Parameters.size() - 1) {
        FunctionPrototype += ", ";
        CallString += ", ";
      }
    }

    // End the function.
    FunctionPrototype += ")";

    std::string FunctionBody = FunctionPrototype + "\n";
    FunctionBody += "{\n";
    FunctionBody += ArgPreprocess;
    FunctionBody += CallString + ");\n";

    if (HasCallback) {
      FunctionBody += "\tretFunc(result______.data(), result______.size(), "
                      "retUserData);\n";
    } else {
      FunctionBody += "\treturn result______;\n";
    }

    FunctionBody += "}\n\n";

    HeaderFile += "extern " + FunctionPrototype + ";\n";
    CppFile += FunctionBody;

    FunctionPointers.push_back(FuncPointerInfo);
  }

  return FunctionPointers;
}

void createFuncPointerTable(std::string &Header, std::string &CppFile,
                            const std::vector<FunctionPointerInfo> &Functions) {
  std::string TableDef = "typedef struct HushFuncPtrTable {\n";
  std::string TableInit = "#ifdef HUSH_STATIC_BINDING\n"
                          "HushFuncPtrTable HUSH_FUNCPTR_TABLE = {\n";
  for (const auto &Func : Functions) {
    std::string FuncPtrName = "HushFuncPtr_" + Func.ExportedName;
    TableDef += "\t" + Func.ReturnType + " (*" + FuncPtrName + ")(";
    for (size_t I = 0; I < Func.Parameters.size(); ++I) {
      TableDef += Func.Parameters[I];
      if (I != Func.Parameters.size() - 1) {
        TableDef += ", ";
      }
    }
    TableDef += ");\n";

    TableInit += "\t" + Func.ExportedName + ",\n";
  }



  TableDef += "\n} HushFuncPtrTable;\n\n";

  Header += TableDef;

  Header += "#ifdef HUSH_STATIC_BINDING\n"
            "extern HushFuncPtrTable HUSH_FUNCPTR_TABLE;\n"
            "#endif\n";

  TableInit += "};\n"
               "#endif\n";

  CppFile += TableInit;
}
