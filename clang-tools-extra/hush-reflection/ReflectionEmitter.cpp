#include "ReflectionEmitter.h"

#include <cstdio>
#include <string>

namespace hush_reflection {
namespace {

std::string escapeCppString(const std::string &Value) {
  std::string Escaped;
  Escaped.reserve(Value.size());
  for (unsigned char C : Value) {
    switch (C) {
    case '\\':
      Escaped += "\\\\";
      break;
    case '"':
      Escaped += "\\\"";
      break;
    case '\n':
      Escaped += "\\n";
      break;
    case '\r':
      Escaped += "\\r";
      break;
    case '\t':
      Escaped += "\\t";
      break;
    default:
      if (C >= 0x20 && C <= 0x7e) {
        Escaped.push_back(static_cast<char>(C));
      } else {
        char Octal[5]{};
        std::snprintf(Octal, sizeof(Octal), "\\%03o", C);
        Escaped += Octal;
      }
      break;
    }
  }
  return Escaped;
}

} // namespace

// ---------------------------------------------------------------------------
// Top-level emit
// ---------------------------------------------------------------------------

void ReflectionEmitter::emit(const ClassModel &Model,
                             llvm::raw_ostream &OS) const {
  emitReflectionCode(Model, OS);
  if (Model.IsSystem) {
    emitSystemCode(Model, OS);
  }
  emitSerializeCode(Model, OS);
  emitDeserializeCode(Model, OS);
  OS << "private:\n";
}

// ---------------------------------------------------------------------------
// Reflection (RegisterReflection + TypeId + TypeName)
// ---------------------------------------------------------------------------

void ReflectionEmitter::emitReflectionCode(const ClassModel &Model,
                                           llvm::raw_ostream &OS) const {
  std::string Out;
  Out.reserve(1024 * 32);

  Out += "#include <reflection/Type.hpp>\n"
         "#include <serialization/Serialization.hpp>\n"
         "#include <serialization/Deserialization.hpp>\n";
  if (Model.IsSystem) {
    // Systems also need the engine system descriptor types.
    Out += "#include <ISystem.hpp>\n"
           "#include <SystemDescriptor.hpp>\n";
  }
  Out += "\n"
         "#ifdef HUSH_GENERATED_BODY\n"
         "#undef HUSH_GENERATED_BODY\n"
         "#endif\n"
         "#ifdef RegisterClass\n"
         "#undef RegisterClass\n"
         "#endif\n"
         "#define HUSH_GENERATED_BODY \\\n"
         "public: \\\n"
         "  static constexpr std::uint64_t TypeId() { return \\\n"
         "Hush::Hashing::Fnv1a64(TypeName()); } \\\n"
         "  static constexpr std::string_view TypeName() { return \"";
  Out += escapeCppString(Model.CanonicalName);
  Out += "\"; }\\\n"
         "  static bool RegisterReflection(Hush::Reflection::ReflectionDB &db, \\\n"
         "Hush::ModuleHandle module = Hush::ENGINE_MODULE_HANDLE) { \\\n"
         "return \\\n";
  Out += "db.RegisterClass<";
  Out += Model.QualifiedName;
  Out += ">()\\\n";

  for (const auto &Ctor : Model.Constructors) {
    Out += emitConstructor(Ctor);
  }

  for (const auto &Func : Model.Functions) {
    Out += emitFunction(Func);
  }

  for (const auto &Field : Model.Fields) {
    Out += emitFieldProperty(Field);
  }

  for (const auto &Meta : Model.Metadata) {
    Out += ".AddMetadata(\"";
    Out += escapeCppString(Meta.Key);
    Out += "\", \"";
    Out += escapeCppString(Meta.Value);
    Out += "\")\\\n";
  }

  // Emit authoritative markers after user metadata so reserved keys cannot be
  // changed by an annotation.
  if (Model.IsBuiltin) {
    Out += ".AddMetadata(Hush::Reflection::METADATA_KEY_BUILTIN.data(), \"true\")\\\n";
  }
  if (Model.IsComponent) {
    Out +=
        ".AddMetadata(Hush::Reflection::METADATA_KEY_COMPONENT.data(), \"true\")\\\n";
  }
  if (Model.IsSystem) {
    Out += ".AddMetadata(Hush::Reflection::METADATA_KEY_SYSTEM.data(), \"true\")\\\n";
  }

  Out += ".Register(module) == Hush::Reflection::ERegisterClassError::None;\\\n";
  Out += "}\\\n";

  OS << Out;
}

// ---------------------------------------------------------------------------
// Systems (factory + descriptor)
// ---------------------------------------------------------------------------

void ReflectionEmitter::emitSystemCode(const ClassModel &Model,
                                       llvm::raw_ostream &OS) const {
  std::string Out;

  // The factory is a static member so it can call the protected SetOrder.
  Out += "  static constexpr std::uint16_t SystemOrder() { return ";
  Out += std::to_string(Model.SystemOrder);
  Out += "; }\\\n"
         "  static Hush::ISystem *CreateSystemInstance(Hush::Scene &scene) { \\\n"
         "    auto *system = new ";
  Out += Model.QualifiedName;
  Out += "(scene); \\\n"
         "    system->SetOrder(SystemOrder()); \\\n"
         "    return system; \\\n"
         "  } \\\n"
         "  static Hush::SystemDescriptor GetSystemDescriptor() { \\\n"
         "    return {Hush::Reflection::TypeId{TypeId()}, TypeName().data(), \\\n"
         "        SystemOrder(), &CreateSystemInstance}; \\\n"
         "  }\\\n";

  OS << Out;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void ReflectionEmitter::emitSerializeCode(const ClassModel &Model,
                                          llvm::raw_ostream &OS) const {
  std::string Out;
  Out.reserve(1024 * 32);

  Out += "template <typename T> Hush::Serialization::ESerializationError "
         "Serialize(T &serializer) "
         "const {\\\n";

  Out += "  auto error = serializer.template "
         "Serialize<std::string_view>(\"__type\", \"";
  Out += escapeCppString(Model.CanonicalName);
  Out += "\");\\\n";
  Out += "  if (error != Hush::Serialization::ESerializationError::"
         "None) {\\\n";
  Out += "    return error;\\\n"
         "  }\\\n";

  for (const auto &Field : Model.Fields) {
    Out += emitFieldSerialize(Field);
  }

  Out += "return Hush::Serialization::ESerializationError::None;\\\n"
         "}\\\n";

  OS << Out;
}

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------

void ReflectionEmitter::emitDeserializeCode(const ClassModel &Model,
                                            llvm::raw_ostream &OS) const {
  const auto &Fields = Model.Fields;
  std::string Out;
  Out.reserve(1024 * 32);

  Out += "auto Deserialize(Hush::Serialization::IVisitor *parent, "
         "Hush::Serialization::EFormatDescribingType format) {\\\n";

  Out += "  struct Visitor : public Hush::Serialization::IVisitor {\\\n";

  for (const auto &Field : Fields) {
    Out += "Hush::Serialization::Visitor<";
    Out += Field.TypeName;
    Out += "> ";
    Out += Field.VisitorFieldName;
    Out += ";\\\n";
  }

  Out += "  enum class EVisitorStatus { None, ";
  for (const auto &Field : Fields) {
    std::string Upper = Field.Name;
    for (auto &C : Upper)
      C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
    Out += Upper;
    Out += ", ";
  }
  Out += "};\\\n";

  Out += "  EVisitorStatus status = EVisitorStatus::None;\\\n";
  Out += "  bool insideObject{false};\\\n";
  Out += "  explicit Visitor(IVisitor *parent, ";
  Out += Model.QualifiedName;
  Out += " *instance, ";
  Out += "Hush::Serialization::EFormatDescribingType format) : "
         "IVisitor(parent, format)";

  if (!Fields.empty()) {
    Out += ", ";
  }

  for (const auto &Field : Fields) {
    Out += Field.VisitorFieldName;
    Out += "(this, &instance->";
    Out += Field.Name;
    Out += ", format), ";
  }

  // Remove the last comma
  if (!Fields.empty()) {
    Out.erase(Out.size() - 2);
  }

  Out += "{\\\n"
         "(void)instance;\\\n";
  Out += "    if (format == "
         "Hush::Serialization::EFormatDescribingType::"
         "NonSelfDescribing) {\\\n";
  Out += "      SetStartingVisitor(";

  if (!Fields.empty()) {
    Out += "&";
    Out += Fields[0].VisitorFieldName;
    Out += ");\\\n";

    for (size_t I = 1; I < Fields.size() - 1; ++I) {
      Out += Fields[I].VisitorFieldName;
      Out += ".SetParentVisitor(&";
      Out += Fields[I - 1].VisitorFieldName;
      Out += ");\\\n";
    }

    if (!Fields.empty()) {
      Out += Fields.back().VisitorFieldName;
      Out += ".SetParentVisitor(GetParentVisitor());\\\n";
    }
  } else {
    Out += "GetParentVisitor());\\\n";
  }

  Out += "    } else {\\\n";
  Out += "      SetStartingVisitor(this);\\\n";
  Out += "    }\\\n";

  Out += "  }\\\n"
         "  Result VisitObjectStart() override {\\\n"
         "    if (insideObject) {\\\n"
         "      return "
         "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
         "    }\\\n"
         "insideObject = true;\\\n"
         "    return this;\\\n"
         "  }\\\n"
         "\\\n";

  Out += "  Result VisitObjectEnd() override {\\\n"
         "   if (!insideObject) {\\\n"
         "      return "
         "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
         "}\\\n"
         "    return GetParentVisitor();\\\n"
         "    }\\\n";

  Out += "  Result VisitKey(std::string_view value) override {\\\n"
         "    (void)value;\\\n"
         "    if (!insideObject) {\\\n"
         "      return "
         "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
         "    }\\\n";

  for (size_t I = 0; I < Fields.size(); ++I) {
    std::string Upper = Fields[I].Name;
    for (auto &C : Upper)
      C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));

    Out += "    if (value == \"";
    Out += Fields[I].Name;
    Out += "\") {\\\n";
    Out += "      status = EVisitorStatus::";
    Out += Upper;
    Out += ";\\\n";
    Out += "      return &";
    Out += Fields[I].VisitorFieldName;
    Out += ";\\\n";
    Out += "    }\\\n";
  }
  Out += "    return this;\\\n"
         "}\\\n";

  Out += "};\\\n"
         "  return Visitor{parent, this, format};\\\n"
         "}\\\n";

  OS << Out;
}

// ---------------------------------------------------------------------------
// Field helpers
// ---------------------------------------------------------------------------

std::string ReflectionEmitter::emitMetadataInitList(
    const std::vector<MetaPair> &Meta) {
  if (Meta.empty()) {
    return "";
  }
  std::string Out = ", {";
  for (size_t I = 0; I < Meta.size(); ++I) {
    Out += "{\"";
    Out += escapeCppString(Meta[I].Key);
    Out += "\", \"";
    Out += escapeCppString(Meta[I].Value);
    Out += "\"}";
    if (I + 1 < Meta.size()) {
      Out += ", ";
    }
  }
  Out += "}";
  return Out;
}

std::string ReflectionEmitter::emitFieldProperty(const FieldModel &Field) {
  std::string Out;
  Out.reserve(8192);

  Out = "    .AddProperty(Hush::Reflection::FieldInfo(\\\n"
        "Hush::Reflection::GetTypeId<std::remove_cv_t<decltype(";
  Out += Field.Name;
  Out += ")>>(), \"";
  Out += Field.Name;
  Out += "\", ";
  Out += emitFieldSetter(Field);
  Out += ", ";
  Out += emitFieldGetter(Field);
  Out += ", ";
  Out += " offsetof(";
  Out += Field.ParentClassName;
  Out += ", ";
  Out += Field.Name;
  Out += ")";
  Out += emitMetadataInitList(Field.Metadata);
  Out += "))\\\n";

  return Out;
}

std::string ReflectionEmitter::emitFieldSerialize(const FieldModel &Field) {
  std::string Out;
  Out.reserve(8192);

  Out = "    if (auto result = serializer.Serialize(\"";
  Out += Field.Name;
  Out += "\", ";
  Out += Field.Name;
  Out += "); result != Hush::Serialization::ESerializationError::None) {\\\n"
         "      return result;\\\n"
         "    }\\\n";

  return Out;
}

std::string ReflectionEmitter::emitFieldSetter(const FieldModel &Field) {
  std::string Out;
  Out.reserve(8192);

  Out = "[](std::span<const Hush::Reflection::VariantView> params) -> "
        "Hush::Reflection::Variant::EVariantError {\\\n";
  Out += "      if (params.size() != 2) {\\\n";
  Out += "        return "
         "Hush::Reflection::Variant::EVariantError::NonSameType;\\\n";
  Out += "      }\\\n";
  Out += "      auto result = params[0].Get<";
  Out += Field.ParentClassName;
  Out += ">();\\\n";
  Out += "      if (result.has_error()) {\\\n";
  Out += "        return result.error();\\\n";
  Out += "      }\\\n";
  Out += "      auto *instance = result.value();\\\n";
  Out += "      auto value = params[1].Get<";
  Out += Field.TypeName;
  Out += ">();\\\n";
  Out += "      if (value.has_error()) {\\\n";
  Out += "        return value.error();\\\n";
  Out += "      }\\\n";

  if (Field.HasCustomSetter) {
    Out += "      instance->" + Field.SetterName + "(*value.value());\\\n";
  } else {
    Out += "      instance->" + Field.Name + " = *value.value();\\\n";
  }

  Out += "      return {};\\\n";
  Out += "    }";

  return Out;
}

std::string ReflectionEmitter::emitFieldGetter(const FieldModel &Field) {
  std::string Out;
  Out.reserve(8192);

  Out = "[](std::span<const Hush::Reflection::VariantView> params) -> "
        "Hush::Result<Hush::Reflection::Variant, "
        "Hush::Reflection::Variant::EVariantError> {\\\n";
  Out += "      if (params.size() != 1) {\\\n";
  Out += "        return "
         "Hush::Reflection::Variant::EVariantError::NonSameType;\\\n";
  Out += "      }\\\n";
  Out += "      auto result = params[0].Get<";
  Out += Field.ParentClassName;
  Out += ">();\\\n";
  Out += "      if (result.has_error()) {\\\n";
  Out += "        return result.error();\\\n";
  Out += "      }\\\n";
  Out += "      auto *instance = result.value();\\\n";

  if (Field.HasCustomGetter) {
    Out += "      return Hush::Reflection::Variant(instance->" +
           Field.GetterName + "());\\\n";
  } else {
    Out += "      return Hush::Reflection::Variant(instance->" + Field.Name +
           ");\\\n";
  }

  Out += "    }";

  return Out;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

std::string
ReflectionEmitter::emitConstructor(const ConstructorModel &Ctor) {
  const auto &Params = Ctor.Params;
  const size_t NumParams = Params.size();

  std::string Code;
  Code.reserve(8192);

  // --- AddConstructor ---
  Code += "   .AddConstructor(Hush::Reflection::FunctionInfo::Create<";
  for (size_t I = 0; I < NumParams; ++I) {
    if (I > 0) {
      Code += ", ";
    }
    Code += Params[I].CanonicalTypeName;
  }
  Code += ">(\\\n";

  Code += "      [](std::span<const Hush::Reflection::VariantView> args) -> "
          "Hush::Result<Hush::Reflection::Variant, "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError> {\\\n";
  Code += "        if (args.size() != ";
  Code += std::to_string(NumParams);
  Code += ") {\\\n"
          "          return "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError::"
          "InvalidArgsCount;\\\n";
  Code += "        }\\\n";

  for (size_t I = 0; I < NumParams; ++I) {
    Code += "        auto param";
    Code += std::to_string(I);
    Code += "Result = args[";
    Code += std::to_string(I);
    Code += "].Get<";
    Code += Params[I].CanonicalTypeName;
    Code += ">();\\\n";
    Code += "        if (!param";
    Code += std::to_string(I);
    Code += "Result.has_value()) {\\\n"
            "          return "
            "Hush::Reflection::FunctionInfo::EFunctionInfoError::InvalidType;"
            "\\\n"
            "}\\\n";
  }

  Code += "        return Hush::Reflection::Variant::CreateInPlace<";
  Code += Ctor.ParentClassName;
  Code += ">(";

  for (size_t I = 0; I < NumParams; ++I) {
    if (Params[I].IsPointer) {
      Code += "param";
      Code += std::to_string(I);
      Code += "Result.value()";
    } else {
      Code += "*param";
      Code += std::to_string(I);
      Code += "Result.value()";
    }
    if (I < NumParams - 1) {
      Code += ", ";
    }
  }

  Code += ");\\\n"
          "      }, \"";
  Code += Ctor.ParentClassName;
  Code += "\""
          "    ))\\\n"
          "   "
          ".AddInPlaceConstructor(Hush::Reflection::TypeInfo::InPlaceCtor::"
          "Create<";

  // --- AddInPlaceConstructor ---
  for (size_t I = 0; I < NumParams; ++I) {
    if (I > 0) {
      Code += ", ";
    }
    Code += Params[I].CanonicalTypeName;
  }
  Code += ">(\\\n";

  Code += "      [](void *mem, std::span<const Hush::Reflection::VariantView> "
          "args) -> Hush::Reflection::TypeInfo::EInPlaceConstructorError {\\\n";
  Code += "        if (args.size() != ";
  Code += std::to_string(NumParams);
  Code += ") {\\\n"
          "          return "
          "Hush::Reflection::TypeInfo::EInPlaceConstructorError::"
          "NonMatchingArgs;\\\n"
          "        }\\\n";

  for (size_t I = 0; I < NumParams; ++I) {
    Code += "        auto param";
    Code += std::to_string(I);
    Code += "Result = args[";
    Code += std::to_string(I);
    Code += "].Get<";
    Code += Params[I].CanonicalTypeName;
    Code += ">();\\\n";
    Code += "        if (!param";
    Code += std::to_string(I);
    Code += "Result.has_value()) {\\\n"
            "          return "
            "Hush::Reflection::TypeInfo::EInPlaceConstructorError::InvalidType;"
            "\\\n"
            "}\\\n";
  }

  Code += "  std::construct_at(static_cast<";
  Code += Ctor.ParentClassName;
  Code += " *>(mem)";
  if (NumParams > 0) {
    Code += ", ";
  }
  for (size_t I = 0; I < NumParams; ++I) {
    if (!Params[I].IsPointer) {
      Code += "*param";
      Code += std::to_string(I);
      Code += "Result.value()";
    } else {
      Code += "param";
      Code += std::to_string(I);
      Code += "Result.value()";
    }
  }

  Code += ");\\\n"
          "        return "
          "Hush::Reflection::TypeInfo::EInPlaceConstructorError::None;\\\n"
          "      }\\\n"
          "    )\\\n";
  Code += "  )\\\n";

  return Code;
}

// ---------------------------------------------------------------------------
// Function
// ---------------------------------------------------------------------------

std::string ReflectionEmitter::emitFunction(const FunctionModel &Func) {
  const auto &Params = Func.Params;
  const size_t NumParams = Params.size();

  std::string Code;
  Code.reserve(8192);

  Code += ".AddFunction(Hush::Reflection::FunctionInfo::Create<";
  Code += Func.ParentClassName;
  if (NumParams > 0) {
    Code += ", ";
  }

  for (size_t I = 0; I < NumParams; ++I) {
    Code += Params[I].TypeName;
    if (I < NumParams - 1) {
      Code += ", ";
    }
  }

  Code += ">(\\\n"
          "[](std::span<const Hush::Reflection::VariantView> args) -> "
          "Hush::Result<Hush::Reflection::Variant, "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError> {\\\n"
          "if (args.size() != ";
  Code += std::to_string(NumParams + 1); // +1 for the instance
  Code += ") {\\\n"
          "return Hush::Reflection::FunctionInfo::EFunctionInfoError::"
          "InvalidArgsCount;\\\n"
          "}\\\n";

  Code += "auto instanceResult = args[0].Get<";
  Code += Func.ParentClassName;
  Code += ">();\\\n"
          "if (instanceResult.has_error()) {\\\n"
          "return "
          "Hush::Reflection::FunctionInfo::EFunctionInfoError::InvalidType;"
          "\\\n"
          "}\\\n"
          "auto *instance = instanceResult.value();\\\n";

  for (size_t I = 0; I < NumParams; ++I) {
    Code += "auto param";
    Code += std::to_string(I);
    Code += "Result = args[";
    Code += std::to_string(I + 1);
    Code += "].Get<";
    Code += Params[I].CanonicalTypeName;
    Code += ">();\\\n"
            "if (param";
    Code += std::to_string(I);
    Code += "Result.has_error()) {\\\n"
            "return Hush::Reflection::FunctionInfo::EFunctionInfoError::"
            "InvalidType;\\\n"
            "}\\\n";
  }

  if (Func.ReturnsVoid) {
    Code += "instance->";
  } else {
    Code += "auto result = ";
  }
  Code += Func.Name;
  Code += "(";

  for (size_t I = 0; I < NumParams; ++I) {
    if (Params[I].IsPointer) {
      Code += "param" + std::to_string(I) + "Result.value()";
    } else {
      Code += "*param" + std::to_string(I) + "Result.value()";
    }
    if (I < NumParams - 1) {
      Code += ", ";
    }
  }

  if (Func.ReturnsVoid) {
    Code += ");\\\n"
            "return Hush::Reflection::Variant();\\\n";
  } else {
    Code += ");\\\n"
            "return Hush::Reflection::Variant(result);\\\n";
  }

  Code += "}, \"";
  Code += Func.Name;
  Code += "\"";
  Code += emitMetadataInitList(Func.Metadata);
  Code += "))\\\n";

  return Code;
}

} // namespace hush_reflection
