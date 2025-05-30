#include "ClassField.h"

ClassField::ClassField(const clang::FieldDecl *fieldDecl,
                       const clang::HushPropertyAttr *propertyAttr)
    : FieldDecl(fieldDecl), PropertyAttr(propertyAttr), FieldGetter(),
      FieldSetter() {

  FieldName = fieldDecl->getName().str();
  FieldTypeName = fieldDecl->getType().getAsString();
  ParentClassName = fieldDecl->getParent()->getNameAsString();
  VisitorFieldName = FieldName;
  VisitorFieldName += "Visitor";

  calculateGetterAndSetter();
}

std::string ClassField::generatePropertyReflectionCode() const {

  std::string ReflectionCode;
  ReflectionCode.reserve(8192);

  ReflectionCode = "    .AddProperty(Hush::Reflection::FieldInfo(\\\n"
                   "Hush::Reflection::GetTypeId<std::remove_cv_t<decltype(";

  ReflectionCode += FieldName;
  ReflectionCode += ")>>(), \"";
  ReflectionCode += FieldName;
  ReflectionCode += "\", ";
  ReflectionCode += generateSetterCode();
  ReflectionCode += ", ";
  ReflectionCode += generateGetterCode();
  ReflectionCode += "))\\\n";

  return ReflectionCode;
}

std::string ClassField::generateSerializeCode() const {
  std::string SerializeCode;
  SerializeCode.reserve(8192);
  SerializeCode = "    if (auto result = serializer.Serialize(\"";
  SerializeCode += FieldName;
  SerializeCode += "\", ";
  SerializeCode += FieldName;
  SerializeCode +=
      "); result != Hush::Serialization::ESerializationError::None) {\\\n"
      "      return result;\\\n"
      "    }\\\n";

  return SerializeCode;
}

std::string ClassField::getVisitorFieldType() const {
  std::string VisitorField = "Hush::Serialization::Visitor<";
  VisitorField += FieldTypeName;
  VisitorField += ">";

  return VisitorField;
}

llvm::StringRef ClassField::getVisitorFieldName() const { return VisitorFieldName; }

llvm::StringRef ClassField::getFieldName() const { return FieldName; }

void ClassField::calculateGetterAndSetter() {
  for (const auto &Expr : PropertyAttr->propertyConfig()) {
    // Is Expr a call to the function Hush::Reflection::Getter?
    if (const auto *CallExpr = llvm::dyn_cast<clang::CallExpr>(Expr)) {
      if (CallExpr->getDirectCallee() &&
          CallExpr->getDirectCallee()->getNameAsString() ==
              "Hush::Reflection::Getter") {
        FieldGetter.IsFunction = true;
        if (CallExpr->getNumArgs() > 0) {
          FieldGetter.Name =
              CallExpr->getArg(0)->getSourceRange().printToString(
                  FieldDecl->getASTContext().getSourceManager());
        }
      }
      if (CallExpr->getDirectCallee() &&
          CallExpr->getDirectCallee()->getNameAsString() ==
              "Hush::Reflection::Setter") {
        FieldSetter.IsFunction = true;
        if (CallExpr->getNumArgs() > 0) {
          FieldSetter.Name =
              CallExpr->getArg(0)->getSourceRange().printToString(
                  FieldDecl->getASTContext().getSourceManager());
        }
      }
    }
  }
}

std::string ClassField::generateSetterCode() const {
  std::string SetterCode;

  SetterCode.reserve(8192);

  SetterCode = "[](std::span<const Hush::Reflection::VariantView> params) -> "
               "Hush::Reflection::Variant::EVariantError {\\\n";

  SetterCode += "      if (params.size() != 2) {\\\n";
  SetterCode += "        return "
                "Hush::Reflection::Variant::EVariantError::NonSameType;\\\n";
  SetterCode += "      }\\\n";
  SetterCode += "      auto result = params[0].Get<";
  SetterCode += ParentClassName;
  SetterCode += ">();\\\n";
  SetterCode += "      if (result.has_error()) {\\\n";
  SetterCode += "        return result.error();\\\n";
  SetterCode += "      }\\\n";
  SetterCode += "      auto *instance = result.value();\\\n";
  SetterCode += "      auto value = params[1].Get<";
  SetterCode += FieldTypeName;
  SetterCode += ">();\\\n";
  SetterCode += "      if (value.has_error()) {\\\n";
  SetterCode += "        return value.error();\\\n";
  SetterCode += "      }\\\n";
  if (FieldSetter.IsFunction) {
    SetterCode +=
        "      instance->" + FieldSetter.Name + "(value.value());\\\n";
  } else {
    SetterCode += "      instance->" + FieldName + " = *value.value();\\\n";
  }
  SetterCode += "      return {};\\\n";
  SetterCode += "    }";

  return SetterCode;
}

std::string ClassField::generateGetterCode() const {
  std::string GetterCode;

  GetterCode.reserve(8192);

  GetterCode = "[](std::span<const Hush::Reflection::VariantView> params) -> "
               "Hush::Result<Hush::Reflection::Variant, "
               "Hush::Reflection::Variant::EVariantError> {\\\n";

  GetterCode += "      if (params.size() != 1) {\\\n";
  GetterCode += "        return "
                "Hush::Reflection::Variant::EVariantError::NonSameType;\\\n";
  GetterCode += "      }\\\n";

  GetterCode += "      auto result = params[0].Get<";
  GetterCode += ParentClassName;
  GetterCode += ">();\\\n";

  GetterCode += "      if (result.has_error()) {\\\n";
  GetterCode += "        return result.error();\\\n";
  GetterCode += "      }\\\n";

  GetterCode += "      auto *instance = result.value();\\\n";
  if (FieldGetter.IsFunction) {
    GetterCode += "      return Hush::Reflection::Variant(instance->" +
                  FieldGetter.Name + "());\\\n";
  } else {
    GetterCode += "      return Hush::Reflection::Variant(instance->" +
                  FieldName + ");\\\n";
  }
  GetterCode += "    }";

  return GetterCode;
}
