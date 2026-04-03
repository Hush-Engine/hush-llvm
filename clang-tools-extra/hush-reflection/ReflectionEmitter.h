#pragma once
#include "ReflectionModel.h"

#include "llvm/Support/raw_ostream.h"

namespace hush_reflection {

class ReflectionEmitter {
public:
  void emit(const ClassModel &Model, llvm::raw_ostream &OS) const;

private:
  void emitReflectionCode(const ClassModel &Model,
                          llvm::raw_ostream &OS) const;
  void emitSerializeCode(const ClassModel &Model,
                         llvm::raw_ostream &OS) const;
  void emitDeserializeCode(const ClassModel &Model,
                           llvm::raw_ostream &OS) const;

  static std::string emitFieldProperty(const FieldModel &Field);
  static std::string emitFieldSerialize(const FieldModel &Field);
  static std::string emitFieldSetter(const FieldModel &Field);
  static std::string emitFieldGetter(const FieldModel &Field);
  static std::string emitConstructor(const ConstructorModel &Ctor);
  static std::string emitFunction(const FunctionModel &Func);
};

} // namespace hush_reflection
