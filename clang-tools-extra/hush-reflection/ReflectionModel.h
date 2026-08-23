#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hush_reflection {

/// One key/value pair from a [[hush::meta]] attribute.
struct MetaPair {
  std::string Key;
  std::string Value;
};

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

  std::vector<MetaPair> Metadata;
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

  std::vector<MetaPair> Metadata;
};

struct ClassModel {
  std::string QualifiedName;   // "Hush::Transform"
  std::string UnqualifiedName; // "Transform"
  /// Canonical name used for the TypeId hash and serialization.
  /// Dotted form of the qualified name (e.g., "Hush.Transform"), or the
  /// explicit override from Hush::Reflection::name("...").
  std::string CanonicalName;
  std::vector<FieldModel> Fields;
  std::vector<FunctionModel> Functions;
  std::vector<ConstructorModel> Constructors;

  // Set when the class is marked with [[hush::system]].
  bool IsSystem = false;
  // Set when the class is marked with [[hush::component]].
  bool IsComponent = false;
  // Set when the class is marked with [[hush::builtin]].
  bool IsBuiltin = false;
  // Update order of the system, from Hush::Reflection::order(n).
  uint32_t SystemOrder = 0;

  std::vector<MetaPair> Metadata;
};

/// A free function marked with [[hush::module_init]]. The generated
/// RegisterModule function calls these after all types are registered.
struct ModuleInitFunction {
  std::string QualifiedName; // "Hush::InitMyModule"
  std::string HeaderPath;    // header where the function is declared
};

} // namespace hush_reflection
