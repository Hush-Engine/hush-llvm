//===-- CodeEmitter.cpp - Emit C binding code from IR ---------------------===//

#include "CodeEmitter.h"

using namespace hush;

void CodeEmitter::emit(const CBindingIR &IR, llvm::raw_ostream &Header,
                       llvm::raw_ostream &Impl) const {
  emitHeaderPreamble(Header);
  emitImplPreamble(Impl);

  // Enums first (types may reference them)
  for (const auto &Enum : IR.enums)
    emitEnum(Enum, Header);

  // Structs
  for (const auto &Struct : IR.structs) {
    emitStruct(Struct, Header);
    if (Struct.needsDestructor)
      emitDestructor(Struct, Header, Impl);
  }

  // Functions
  for (const auto &Func : IR.functions) {
    emitFunctionDecl(Func, Header);
    emitFunctionImpl(Func, Impl);
  }

  // Function pointer table
  emitFuncPtrTable(IR.functions, Header, Impl);

  emitHeaderPostamble(Header);
}

void CodeEmitter::emitHeaderPreamble(llvm::raw_ostream &OS) const {
  OS << "#pragma once\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n\n\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n\n";
}

void CodeEmitter::emitHeaderPostamble(llvm::raw_ostream &OS) const {
  OS << "\n#ifdef __cplusplus\n"
        "}\n"
        "#endif\n";
}

void CodeEmitter::emitImplPreamble(llvm::raw_ostream &OS) const {
  OS << "// Auto-generated file\n"
        "// DO NOT EDIT\n\n"
        "#include \"bindings.hpp\"\n"
        "#include \"HushBindings.h\"\n\n";
}

// ---- Enums ----

void CodeEmitter::emitEnum(const CEnumDef &Enum, llvm::raw_ostream &Header) const {
  if (Enum.isPlainEnum) {
    Header << "typedef enum " << Enum.name << " {\n";
    for (const auto &Val : Enum.values)
      Header << "\t" << Enum.name << "_" << Val.name << " = " << Val.value
             << ",\n";
    Header << "} " << Enum.name << ";\n";
  } else {
    Header << "typedef " << Enum.underlyingType << " " << Enum.name << ";\n";
    for (const auto &Val : Enum.values)
      Header << "#define " << Enum.name << "_" << Val.name << " " << Val.value
             << "\n";
  }
  Header << "\n";
}

// ---- Structs ----

void CodeEmitter::emitStruct(const CStruct &Struct, llvm::raw_ostream &Header) const {
  if (Struct.isOpaque) {
    Header << "typedef struct " << Struct.name << " " << Struct.name << ";\n";
    return;
  }

  Header << "typedef struct " << Struct.name << " {\n";
  for (const auto &Field : Struct.fields) {
    if (Field.isOpaque) {
      Header << "\talignas(" << Field.opaqueAlign << ") char " << Field.name
             << "[" << Field.opaqueSize << "];\n";
    } else if (!Field.funcPointerDeclWithName.empty()) {
      Header << "\t" << Field.funcPointerDeclWithName << ";\n";
    } else {
      Header << "\t" << Field.type.toString() << " " << Field.name << ";\n";
    }
  }
  Header << "} " << Struct.name << ";\n\n";
}

void CodeEmitter::emitDestructor(const CStruct &Struct,
                                 llvm::raw_ostream &Header,
                                 llvm::raw_ostream &Impl) const {
  std::string FuncName = Struct.name + "_destroy";
  std::string ParamType = Struct.name + " **self";

  Header << "void " << FuncName << "(" << ParamType << ");\n";

  Impl << "void " << FuncName << "(" << ParamType << ")\n{\n";
  Impl << "\t" << Struct.cppName << " *selfClass = reinterpret_cast<"
       << Struct.cppName << " *>(*self);\n";
  Impl << "\tif (selfClass != nullptr)\n\t{\n";
  Impl << "\t\t selfClass->~" << Struct.cppUnqualifiedName << "();\n\t}\n";
  Impl << "\t*self = nullptr;\n";
  Impl << "}\n\n";
}

// ---- Functions ----

std::string CodeEmitter::buildSignature(const CFunction &Func) const {
  std::string Sig;
  llvm::raw_string_ostream OS(Sig);

  OS << Func.returnType.toString() << " " << Func.name << "(";

  bool First = true;

  // Callback params go first (for container returns)
  if (Func.returnMode == ReturnMode::Callback) {
    OS << "void (*retFunc)(" << Func.callbackInnerType
       << "*, size_t, void*), void* retUserData";
    First = false;
  }

  // Self parameter for member functions
  if (Func.isMemberFunction) {
    if (!First)
      OS << ", ";
    OS << Func.selfCType << " *self";
    First = false;
  }

  // Regular parameters
  for (const auto &Param : Func.params) {
    if (!First)
      OS << ", ";
    First = false;

    if (Param.isSpanParts) {
      // Expand to two params: T* <name>Data, const size_t <name>Size
      OS << Param.type.toString() << " *" << Param.name << "Data, const size_t "
         << Param.name << "Size";
    } else {
      OS << Param.type.toParamDecl(Param.name);
    }
  }

  if (First) {
    // No parameters at all
    OS << "void";
  }

  OS << ")";
  OS.flush();
  return Sig;
}

void CodeEmitter::emitFunctionDecl(const CFunction &Func,
                                   llvm::raw_ostream &Header) const {
  Header << "extern " << buildSignature(Func) << ";\n";
}

void CodeEmitter::emitFunctionImpl(const CFunction &Func,
                                   llvm::raw_ostream &Impl) const {
  Impl << buildSignature(Func) << "\n{\n";
  emitWrapperBody(Func, Impl);
  Impl << "}\n\n";
}

void CodeEmitter::emitWrapperBody(const CFunction &Func,
                                  llvm::raw_ostream &OS) const {
  // Cast self for member functions
  if (Func.isMemberFunction) {
    OS << "\tauto selfClass = reinterpret_cast<" << Func.selfCppType
       << "*>(self);\n";
  }

  // Preprocess parameters that need conversion
  for (const auto &Param : Func.params) {
    if (Param.mode == PassMode::SpanFromParts) {
      OS << "\tauto " << Param.name << "Data__ = " << Param.cppContainerType
         << "(reinterpret_cast<" << Param.cppInnerRealType << "*>("
         << Param.name << "Data), " << Param.name << "Size);\n";
    }
  }

  // Build the call expression
  bool ReturnsValue = (Func.returnMode != ReturnMode::Void);
  // Callback functions also call the C++ function and capture the result
  bool NeedsResult =
      ReturnsValue || Func.returnMode == ReturnMode::Callback;

  OS << "\t";
  if (NeedsResult)
    OS << "auto result______ = ";

  if (Func.isMemberFunction)
    OS << "selfClass->" << Func.cppMethodName << "(";
  else
    OS << Func.cppName << "(";

  // Emit call arguments
  bool First = true;
  for (const auto &Param : Func.params) {
    if (!First)
      OS << ", ";
    First = false;

    switch (Param.mode) {
    case PassMode::Direct:
      OS << Param.name;
      break;
    case PassMode::Reinterpret:
      OS << "reinterpret_cast<" << Param.cppCastType << ">(" << Param.name
         << ")";
      break;
    case PassMode::StaticCastEnum:
      OS << "static_cast<" << Param.cppCastType << ">(" << Param.name << ")";
      break;
    case PassMode::DerefReinterpret:
      OS << "*reinterpret_cast<" << Param.cppCastType << ">(" << Param.name
         << ")";
      break;
    case PassMode::SpanFromParts:
      OS << Param.name << "Data__";
      break;
    }
  }

  OS << ");\n";

  // Handle return value conversion
  switch (Func.returnMode) {
  case ReturnMode::Void:
    break;
  case ReturnMode::Direct:
    OS << "\treturn result______;\n";
    break;
  case ReturnMode::Callback:
    OS << "\tretFunc(reinterpret_cast<" << Func.callbackInnerType
       << "*>(result______.data()), result______.size(), retUserData);\n";
    break;
  case ReturnMode::PlacementNew:
    OS << "\tstd::aligned_storage_t<sizeof(" << Func.cppReturnType
       << ")> resultStorage_____;\n";
    OS << "\tauto *resultPtr = reinterpret_cast<decltype(result______)*>("
          "&resultStorage_____);\n";
    OS << "\tnew (resultPtr) decltype(result______)(std::move(result______));\n";
    OS << "\treturn *reinterpret_cast<" << Func.returnType.toString()
       << "*>(resultPtr);\n";
    break;
  case ReturnMode::ReinterpretPtr:
    OS << "\treturn reinterpret_cast<" << Func.returnType.toString()
       << ">(result______);\n";
    break;
  case ReturnMode::DerefReinterpret:
    OS << "\treturn *reinterpret_cast<" << Func.returnType.toString()
       << "*>(&result______);\n";
    break;
  case ReturnMode::StaticCastEnum:
    OS << "\treturn static_cast<" << Func.returnType.toString()
       << ">(result______);\n";
    break;
  }
}

// ---- Function pointer table ----

void CodeEmitter::emitFuncPtrTable(const std::vector<CFunction> &Funcs,
                                   llvm::raw_ostream &Header,
                                   llvm::raw_ostream &Impl) const {
  Header << "typedef struct HushFuncPtrTable {\n";

  std::string TableInit = "#ifdef HUSH_STATIC_BINDING\n"
                          "HushFuncPtrTable HUSH_FUNCPTR_TABLE = {\n";

  for (const auto &Func : Funcs) {
    std::string PtrName = "HushFuncPtr_" + Func.name;

    // Build the function pointer type
    Header << "\t" << Func.returnType.toString() << " (*" << PtrName << ")(";

    bool First = true;

    if (Func.returnMode == ReturnMode::Callback) {
      Header << "void (*retFunc)(" << Func.callbackInnerType
             << "*, size_t, void*), void* retUserData";
      First = false;
    }

    if (Func.isMemberFunction) {
      if (!First)
        Header << ", ";
      Header << Func.selfCType << " *self";
      First = false;
    }

    for (const auto &Param : Func.params) {
      if (!First)
        Header << ", ";
      First = false;

      if (Param.isSpanParts) {
        Header << Param.type.toString() << "*, const size_t " << Param.name
               << "Size";
      } else {
        Header << Param.type.toString();
      }
    }

    if (First)
      Header << "void";

    Header << ");\n";

    TableInit += "\t" + Func.name + ",\n";
  }

  Header << "\n} HushFuncPtrTable;\n\n";

  Header << "#ifdef HUSH_STATIC_BINDING\n"
            "extern HushFuncPtrTable HUSH_FUNCPTR_TABLE;\n"
            "#endif\n";

  TableInit += "};\n"
               "#endif\n";

  Impl << TableInit;
}
