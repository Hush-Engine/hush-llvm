//===-- CodeEmitter.h - Emit C binding code from IR -----------------------===//
//
// Takes a fully resolved CBindingIR and writes the C header and
// C++ implementation files.
//
//===----------------------------------------------------------------------===//

#ifndef HUSH_EXPORT_CODE_EMITTER_H
#define HUSH_EXPORT_CODE_EMITTER_H

#include "CBindingIR.h"
#include "llvm/Support/raw_ostream.h"

namespace hush {

class CodeEmitter {
public:
  /// Emit the complete header and implementation files from the IR.
  void emit(const CBindingIR &IR, llvm::raw_ostream &Header,
            llvm::raw_ostream &Impl) const;

private:
  void emitHeaderPreamble(llvm::raw_ostream &OS) const;
  void emitHeaderPostamble(llvm::raw_ostream &OS) const;
  void emitImplPreamble(llvm::raw_ostream &OS) const;

  void emitTypeAlias(const CTypeAlias &Alias, llvm::raw_ostream &Header) const;

  void emitEnum(const CEnumDef &Enum, llvm::raw_ostream &Header) const;

  void emitStruct(const CStruct &Struct, llvm::raw_ostream &Header) const;

  void emitDestructor(const CStruct &Struct, llvm::raw_ostream &Header,
                      llvm::raw_ostream &Impl) const;

  void emitFunctionDecl(const CFunction &Func,
                        llvm::raw_ostream &Header) const;

  void emitFunctionImpl(const CFunction &Func, llvm::raw_ostream &Impl) const;

  /// Write the function signature (shared between decl and def).
  /// Returns the signature string.
  std::string buildSignature(const CFunction &Func) const;

  /// Write the wrapper body that calls the real C++ function.
  void emitWrapperBody(const CFunction &Func, llvm::raw_ostream &OS) const;

  void emitFuncPtrTable(const std::vector<CFunction> &Funcs,
                        llvm::raw_ostream &Header,
                        llvm::raw_ostream &Impl) const;
};

} // namespace hush

#endif // HUSH_EXPORT_CODE_EMITTER_H
