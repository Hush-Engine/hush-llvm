#pragma once
#include <string>
#include <vector>

namespace hush_reflection {

struct ParamModel {
  std::string TypeName;          // as-written (e.g., "const glm::vec3 &")
  std::string CanonicalTypeName; // non-ref canonical (e.g., "glm::vec3")
  bool IsPointer = false;
};

struct FieldModel {
  std::string Name;            // "position"
  std::string TypeName;        // "glm::vec3"
  std::string ParentClassName; // "Hush::Transform"
  std::string VisitorFieldName; // "positionVisitor"

  bool HasCustomGetter = false;
  std::string GetterName;
  bool HasCustomSetter = false;
  std::string SetterName;
};

struct ConstructorModel {
  std::string ParentClassName;
  std::vector<ParamModel> Params;
};

struct FunctionModel {
  std::string Name;
  std::string ParentClassName;
  std::vector<ParamModel> Params;
  bool ReturnsVoid = false;
};

struct ClassModel {
  std::string QualifiedName;   // "Hush::Transform"
  std::string UnqualifiedName; // "Transform"
  std::vector<FieldModel> Fields;
  std::vector<FunctionModel> Functions;
  std::vector<ConstructorModel> Constructors;
};

} // namespace hush_reflection
