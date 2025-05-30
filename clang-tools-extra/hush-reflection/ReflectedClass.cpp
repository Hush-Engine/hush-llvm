#include "ReflectedClass.h"

ReflectedClass::ReflectedClass(const clang::CXXRecordDecl *decl)
    : m_decl(decl) {
  this->m_className = decl->getNameAsString();

  Fields = this->getFields();
}

bool ReflectedClass::generateReflectionCode(llvm::raw_ostream &os) const {

  std::string ReflectionData;
  ReflectionData.reserve(1024 * 32);

  ReflectionData +=
      "#include <reflection/Type.hpp>\n"
      "#include <serialization/Serialization.hpp>\n"
      "#include <serialization/Deserialization.hpp>\n\n"
      "#ifdef HUSH_GENERATED_BODY\n"
      "#undef HUSH_GENERATED_BODY\n"
      "#endif\n\n"

      "#define HUSH_GENERATED_BODY \\\n"
      "public: \\\n"
      "  static constexpr std::uint64_t TypeId() { return \\\n"
      "Hush::Hashing::Fnv1a64(TypeName()); } \\\n"
      "  static constexpr std::string_view TypeName() { return \"";
  ReflectionData += this->m_className;
  ReflectionData +=
      "\"; }\\\n"
      "  static void RegisterReflection(Hush::Reflection::ReflectionDB &db) { "
      "\\\n";
  ReflectionData += "db.RegisterClass<";
  ReflectionData += this->m_className;
  ReflectionData += ">()\\\n";

  for (const auto &Field : Fields) {
    ReflectionData += Field.generatePropertyReflectionCode();
  }
  ReflectionData += ".Register();\\\n";
  ReflectionData += "}\\\n";

  os << ReflectionData;

  return true;
}
bool ReflectedClass::generateSerializeCode(llvm::raw_ostream &os) const {

  std::string SerializeData;
  SerializeData.reserve(1024 * 32);
  SerializeData += "template <typename T> Hush::Serialization::ESerializationError "
                   "Serialize(T &serializer) "
                   "const {\\\n";

  SerializeData +=
      "  auto error = serializer.template Serialize<std::string_view>(\"__type\", \"";
  SerializeData += this->m_className;
  SerializeData += "\");\\\n";
  SerializeData += "  if (error != Hush::Serialization::ESerializationError::"
                   "None) {\\\n";
  SerializeData += "    return error;\\\n"
                   "  }\\\n";

  for (const auto &Field : Fields) {
    SerializeData += Field.generateSerializeCode();
  }

  SerializeData += "return Hush::Serialization::ESerializationError::None;\\\n"
                   "}\\\n";

  os << SerializeData;

  return true;
}

bool ReflectedClass::generateDeserializeCode(llvm::raw_ostream &os) const {
  std::string DeserializeData;
  DeserializeData.reserve(1024 * 32);

  DeserializeData += "auto Deserialize(Hush::Serialization::IVisitor *parent, "
                     "Hush::Serialization::EFormatDescribingType format) {\\\n";

  DeserializeData +=
      "  struct Visitor : public Hush::Serialization::IVisitor {\\\n";

  for (const auto &Field : Fields) {
    DeserializeData += Field.getVisitorFieldType();
    DeserializeData += " ";
    DeserializeData += Field.getVisitorFieldName();
    DeserializeData += ";\\\n";
  }
  DeserializeData += "  enum class EVisitorStatus { None, ";
  for (const auto &Field : Fields) {
    DeserializeData += Field.getFieldName().upper();
    DeserializeData += ", ";
  }
  DeserializeData += "};\\\n";

  DeserializeData += "  EVisitorStatus status = EVisitorStatus::None;\\\n";
  DeserializeData += "  bool insideObject{false};\\\n";
  DeserializeData += "  explicit Visitor(IVisitor *parent, ";
  DeserializeData += this->m_className;
  DeserializeData += " *instance, ";
  DeserializeData += "Hush::Serialization::EFormatDescribingType format) : "
                     "IVisitor(parent, format), ";

  for (const auto &Field : Fields) {
    DeserializeData += Field.getVisitorFieldName();
    DeserializeData += "(this, &instance->";
    DeserializeData += Field.getFieldName();
    DeserializeData += ", format), ";
  }
  // Remove the last comma
  if (!Fields.empty()) {
    DeserializeData.erase(DeserializeData.size() - 2);
  }
  DeserializeData += "{\\\n";
  DeserializeData += "    if (format == "
                     "Hush::Serialization::EFormatDescribingType::"
                     "NonSelfDescribing) {\\\n";
  DeserializeData += "      SetStartingVisitor(&";

  if (!Fields.empty()) {
    DeserializeData += Fields[0].getVisitorFieldName();
    DeserializeData += ");\\\n";
  }

  for (size_t I = 1; I < Fields.size() - 1; ++I) {
    DeserializeData += Fields[I].getVisitorFieldName();
    DeserializeData += ".SetParentVisitor(&";
    DeserializeData += Fields[I - 1].getVisitorFieldName();
    DeserializeData += ");\\\n";
  }

  // Get the last field
  if (!Fields.empty()) {
    DeserializeData += Fields.back().getVisitorFieldName();
    DeserializeData += ".SetParentVisitor(GetParentVisitor());\\\n";
  }

  DeserializeData += "    } else {\\\n";
  DeserializeData += "      SetStartingVisitor(this);\\\n";
  DeserializeData += "    }\\\n";

  DeserializeData +=
      "  }\\\n"
      "  Result VisitObjectStart() override {\\\n"
      "    if (insideObject) {\\\n"
      "      return "
      "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
      "    }\\\n"
      "insideObject = true;\\\n"
      "    return this;\\\n"
      "  }\\\n"
      "\\\n";

  DeserializeData +=
      "  Result VisitObjectEnd() override {\\\n"
      "   if (!insideObject) {\\\n"
      "      return "
      "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
      "}\\\n"
      "    return GetParentVisitor();\\\n"
      "    }\\\n";

  DeserializeData +=
      "  Result VisitKey(std::string_view value) override {\\\n"
      "    if (!insideObject) {\\\n"
      "      return "
      "Hush::Serialization::EDeserializationError::InvalidData;\\\n"
      "    }\\\n";

  for (size_t I = 0; I < Fields.size(); ++I) {
    DeserializeData += "    if (value == \"";
    DeserializeData += Fields[I].getFieldName();
    DeserializeData += "\") {\\\n";
    DeserializeData += "      status = EVisitorStatus::";
    DeserializeData += Fields[I].getFieldName().upper();

    DeserializeData += ";\\\n";
    DeserializeData += "      return &";
    DeserializeData += Fields[I].getVisitorFieldName();
    DeserializeData += ";\\\n";
    DeserializeData += "    }\\\n";
  }
  DeserializeData += "    return this;\\\n"
                     "}\\\n";

  DeserializeData += "};\\\n"
                     "  return Visitor{parent, this, format};\\\n"
                     "}\\\n";

  os << DeserializeData;

  return true;
}

std::vector<ClassField> ReflectedClass::getFields() const {
  std::vector<ClassField> Fields;
  for (const auto *Field : this->m_decl->fields()) {
    if (const auto *Property = Field->getAttr<clang::HushPropertyAttr>()) {
      Fields.emplace_back(Field, Property);
    }
  }

  return Fields;
}