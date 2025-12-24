// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ExtractAPI/API.h>
#include <string_view>

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BranchProbability.h"

#include "BindingsGenerator.h"

using namespace clang::tooling;
using namespace llvm;
using namespace clang::ast_matchers;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("hush-export options");

static cl::opt<std::string> CompilerTarget(
  "compiler-target",
  cl::desc("The compiler's compatibility mode: 'clang' or 'msvc' (defaults to clang)"),
  cl::value_desc("mode"),
  cl::init("clang"),
  cl::cat(MyToolCategory)
);

DeclarationMatcher HushExportAttrMatcher =
    decl(recordDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

DeclarationMatcher HushExportFunctionMatcher =
    decl(functionDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

DeclarationMatcher HushExportEnumMatcher =
    decl(enumDecl(hasAttr(clang::attr::HushExport))).bind("hushExportable");

struct FunctionPointerInfo {
  std::string Name;
  std::string ExportedName;
  std::string ReturnType;
  std::vector<std::string> Parameters;
};

static void processClassHeader(
    std::string &HeaderFile, std::string &CppFile,
    std::vector<std::shared_ptr<hush::ExportedClass>> &ParsedClassesVector);

static std::vector<FunctionPointerInfo>
processFunctions(std::string &HeaderFile, std::string &CppFile,
                 std::vector<hush::FunctionInfo> &FunctionInfo);

static void
createFuncPointerTable(std::string &Header, std::string &CppFile,
                       const std::vector<FunctionPointerInfo> &Functions);

static void
processParsedEnums(std::string &Header,
                   std::vector<std::shared_ptr<hush::EnumDeclaration>> &Enums);

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
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

  std::string HeaderFile = "#pragma once\n"
                           "#include <stdint.h>\n\n\n"
                           "#ifdef __cplusplus\n"
                           "typedef bool _Bool;\n"
                           "extern \"C\" {\n"
                           "#endif\n\n";

  std::string CppFile = "// Auto-generated file\n"
                        "// DO NOT EDIT\n\n"
                        "#include \"bindings.hpp\"\n"
                        "#include \"HushBindings.h\"\n\n";

  hush::HushBindingMatcher BindingMatcher;

  MatchFinder Finder;
  Finder.addMatcher(HushExportEnumMatcher, &BindingMatcher);
  Finder.addMatcher(HushExportAttrMatcher, &BindingMatcher);
  Finder.addMatcher(HushExportFunctionMatcher, &BindingMatcher);

  auto Result = Tool.run(newFrontendActionFactory(&Finder).get());

  if (Result != 0) {
    llvm::errs() << "Error processing source files\n";
    return Result;
  }

  processParsedEnums(HeaderFile, BindingMatcher.getParsedEnums());

  auto &ParsedClassesVector = BindingMatcher.getParsedClassesVector();

  // We need to process each one of the classes
  processClassHeader(HeaderFile, CppFile, ParsedClassesVector);

  auto &Functions = BindingMatcher.getFunctions();
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
    std::string &HeaderFile, std::string &CppFile,
    std::vector<std::shared_ptr<hush::ExportedClass>> &ParsedClassesVector) {
  for (auto &ParsedClass : ParsedClassesVector) {
    if (ParsedClass->IsHandle) {
      HeaderFile += "typedef struct " + ParsedClass->ExportedName + " " +
                    ParsedClass->ExportedName + ";\n";
    } else {
      // Process the class
      HeaderFile += "typedef struct " + ParsedClass->ExportedName + " {\n";
      for (auto &Member : ParsedClass->Members) {
        if (!Member.IsHidden && !Member.IsFunctionPointer) {
          HeaderFile += "\t" + Member.Type + " " + Member.Name + ";\n";
        } else if (!Member.IsHidden && Member.IsFunctionPointer) {
          // void * (*) (params...)
          // Search for the first (
          size_t FirstParen = Member.Type.find(')');
          std::string FieldName = Member.Type;
          if (FirstParen != Member.Type.npos) {
            FieldName.replace(FirstParen, 1, Member.Name + ")");
          }
          HeaderFile += "\t" + FieldName + ";\n";
        } else {
          HeaderFile += "\talignas(" + std::to_string(Member.Alignment) +
                        ") char " + Member.Name + "[" +
                        std::to_string(Member.Size) + "];\n";
        }
      }
      HeaderFile += "} " + ParsedClass->ExportedName + ";\n\n";
    }

    if (ParsedClass->IsNonPOD) {
      // Get the type without the namespaces
      std::string ClassName = ParsedClass->Name;
      size_t LastColon = ClassName.find_last_of("::");
      if (LastColon != std::string::npos) {
        ClassName = ClassName.substr(LastColon + 1);
      }
      // We must add a destructor for the class
      HeaderFile += "void " + ParsedClass->ExportedName + "_destroy(" +
                    ParsedClass->ExportedName + " **self);\n";

      CppFile += "void " + ParsedClass->ExportedName + "_destroy(" +
                 ParsedClass->ExportedName + " **self)\n{\n";
      CppFile += "\t" + ParsedClass->Name + " *selfClass = reinterpret_cast<" +
                 ParsedClass->Name + " *>(*self);\n";
      CppFile += "\tif (selfClass != nullptr)\n\t{\n";
      CppFile += "\t\t selfClass->~" + ClassName + "();\n\t}\n";
      // Memory is freed by the caller
      CppFile += "\t*self = nullptr;\n";
      CppFile += "}\n\n";
    }
  }
}

static std::vector<FunctionPointerInfo>
processFunctions(std::string &HeaderFile, std::string &CppFile,
                 std::vector<hush::FunctionInfo> &FunctionInfo) {

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

        FunctionPrototype += "void (*retFunc)(" + InnerType + "* " +
                             ", size_t, void*), void* retUserData";

        FuncPointerInfo.Parameters.push_back("void (*retFunc)(" + InnerType +
                                             "*, size_t, void*)");
        FuncPointerInfo.Parameters.push_back("void* retUserData");

        if (Function.Parameters.size() > 0 ||
            Function.ContainingClass.has_value()) {
          FunctionPrototype += ", ";
        }

        HasCallback = true;
      }
    } else {

      std::string ReturnType;
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

      if (Function.ReturnType.Type == "void") {
        CallString += "\tselfClass->" + FunctionName + "(";
      } else {
        CallString += "\tauto result______ = selfClass->" + FunctionName + "(";
      }
    } else {
      if (Function.ReturnType.Type == "void") {
        CallString += "\t" + Function.Name + "(";
      } else {
        CallString += "\tauto result______ = " + Function.Name + "(";
      }
    }

    for (size_t I = 0; I < Function.Parameters.size(); ++I) {
      auto &Param = Function.Parameters[I];

      if (Param.InnerType.Type.size() > 0) {
        // Check if the type is std::span
        if (Param.Type.find("std::span") != std::string::npos ||
            Param.Type.find("std::string_view") != std::string::npos) {
          ArgPreprocess += "\tauto " + Param.Name + "Data__ = " + Param.Type +
                           "(reinterpret_cast<" + Param.InnerType.RealType + "*>(" + Param.Name + "Data), " + Param.Name + "Size);\n";
          std::string InnerType = Param.InnerType.Type;

          if (Param.Type.find("std::uint") != std::string::npos ||
              Param.Type.find("std::int") != std::string::npos) {
            InnerType.erase(0, 5);
          }

          FunctionPrototype += InnerType + " *" + Param.Name +
                               "Data, const size_t " + Param.Name + "Size";

          FuncPointerInfo.Parameters.push_back(InnerType + "* ");

          FuncPointerInfo.Parameters.push_back("const size_t " + Param.Name +
                                               "Size");

          CallString += Param.Name + "Data__";
        }
      } else {
        std::string ParamType = Param.Type;

        if (Param.Type.find("std::uint") != std::string::npos ||
            Param.Type.find("std::int") != std::string::npos) {
          ParamType.erase(0, 5);
        };

        FuncPointerInfo.Parameters.push_back(ParamType);

        FunctionPrototype += ParamType + " " + Param.Name;

        if (Param.EnumType.length() > 0) {
          CallString +=
              "static_cast<" + Param.EnumType + ">(" + Param.Name + ")";
        } else if (Param.IsReference) {
          // We need to cast the pointer to the reference
          CallString +=
              "*reinterpret_cast<" + Param.RealType + ">(" + Param.Name + ")";
        } else if (Param.IsPointer) {
          CallString +=
              "reinterpret_cast<" + Param.RealType + ">(" + Param.Name + ")";
        } else {
          CallString += Param.Name;
        }
      }

      if (I != Function.Parameters.size() - 1) {
        FunctionPrototype += ", ";
        CallString += ", ";
      }
    }

    if (Function.Parameters.size() == 0 &&
        !Function.ContainingClass.has_value() && !HasCallback) {
      FunctionPrototype += "void";
    }

    // End the function.
    FunctionPrototype += ")";

    std::string FunctionBody = FunctionPrototype + "\n";
    FunctionBody += "{\n";
    FunctionBody += ArgPreprocess;
    FunctionBody += CallString + ");\n";

    if (HasCallback) {
      FunctionBody += "\tretFunc(reinterpret_cast<" +
                      Function.ReturnType.InnerType +
                      "*>(result______.data()), "
                      "result______.size(), retUserData);\n";
    } else if (Function.ReturnType.IsEnum) {
      FunctionBody += "\treturn static_cast<" + Function.ReturnType.Type +
                      ">(result______);\n";
    } else if (Function.ReturnType.Type != "void") {
      if (Function.ReturnType.Type.find("*") != std::string::npos) {
        FunctionBody += "\treturn reinterpret_cast<" +
                        Function.ReturnType.Type + ">(result______);\n";
      } else if (Function.ReturnType.IsReference) {
        FunctionBody += "\treturn *reinterpret_cast<" +
                        Function.ReturnType.Type + ">(result______);\n";
      } else {
        // Not a pointer or reference, just return the result, construct
        // in-place
        FunctionBody += "\treturn *reinterpret_cast<" +
                        Function.ReturnType.Type + "*>(&result______);\n";
      }
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

    if (Func.Parameters.size() == 0) {
      TableDef += "void";
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

void processParsedEnums(
    std::string &Header,
    std::vector<std::shared_ptr<hush::EnumDeclaration>> &Enums) {
  for (auto &Enum : Enums) {
    if (Enum->IsPlainEnum) {
      // Export as an enum
      Header += "typedef enum " + Enum->ExportedName + " {\n";
      for (auto &EnumValue : Enum->EnumValues) {
        Header += "\t" + Enum->ExportedName + "_" + EnumValue.Name + " = " +
                  std::to_string(EnumValue.Value) + ",\n";
      }

      Header += "} " + Enum->ExportedName + ";\n";
    } else {
      std::string InnerType = Enum->InnerType;
      // If the inner type is std::int* or std::uint*, we need to remove the
      // std:: part
      if (Enum->InnerType.find("std::uint") != std::string::npos ||
          Enum->InnerType.find("std::int") != std::string::npos) {
        InnerType.erase(0, 5);
      }
      // Export as a typedef for an int of the underlying type
      Header += "typedef " + InnerType + " " + Enum->ExportedName + ";\n";

      // Make constants for each one of the enum values
      for (auto &EnumValue : Enum->EnumValues) {
        Header += "#define " + Enum->ExportedName + "_" + EnumValue.Name + " " +
                  std::to_string(EnumValue.Value) + "\n";
      }
    }
    Header += "\n";
  }
}
