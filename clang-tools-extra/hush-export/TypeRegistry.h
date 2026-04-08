//===-- TypeRegistry.h - Central type resolution for C bindings -----------===//
//
// The TypeRegistry is the single source of truth for mapping C++ types
// to their C binding equivalents. It replaces the scattered
// find("std::span") / find("glm::") checks throughout the codebase.
//
// Usage:
//   TypeRegistry registry;
//   registry.registerType("glm::vec3", CType::makeStruct("Vector3"));
//   auto resolved = registry.resolve(someQualType);
//
//===----------------------------------------------------------------------===//

#ifndef HUSH_EXPORT_TYPE_REGISTRY_H
#define HUSH_EXPORT_TYPE_REGISTRY_H

#include "CBindingIR.h"

#include "clang/AST/Type.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hush {

/// The result of resolving a C++ type through the registry.
/// Contains everything needed to generate correct parameter/return handling.
struct TypeResolution {
  CType cType;

  /// The original C++ type name (fully qualified).
  std::string cppName;

  /// True if this is a container type (std::span, std::vector, etc.)
  /// that needs special handling: split into (data, size) for params,
  /// callback for returns.
  bool isContainer = false;

  /// For containers: the C type of the element.
  CType containerElementCType;

  /// For containers: the C++ type of the element (for reinterpret_cast).
  std::string containerElementCppType;

  /// For containers: the full C++ container type (for construction).
  /// e.g., "std::span<const int>"
  std::string containerCppType;

  /// True if the resolved type is an enum that needs static_cast.
  bool isEnum = false;

  /// For enums: the C++ enum type for static_cast.
  std::string enumCppType;
};

/// Abstract interface for type translators.
/// Each translator handles one category of C++ type and produces a
/// TypeResolution. The registry tries translators in order until one claims
/// the type.
class TypeTranslator {
public:
  virtual ~TypeTranslator() = default;

  /// Return true if this translator handles the given type.
  virtual bool canTranslate(clang::QualType Type,
                            const class TypeRegistry &Registry) const = 0;

  /// Produce the C binding resolution for this type.
  /// Only called if canTranslate() returned true.
  virtual TypeResolution translate(clang::QualType Type,
                                   const class TypeRegistry &Registry) const = 0;
};

/// Central registry for type mappings and translator dispatch.
class TypeRegistry {
public:
  /// Register a known type mapping by C++ name.
  /// This is used for pre-registered types (glm, exported classes/enums).
  void registerType(const std::string &cppName, CType cType,
                    bool isEnum = false);

  /// Look up a registered type by its C++ name.
  /// Returns nullopt if the type is not registered.
  std::optional<TypeResolution> lookup(const std::string &cppName) const;

  /// Check if a type with this C++ name is registered.
  bool isRegistered(const std::string &cppName) const;

  /// Add a translator to the chain. Translators are tried in the order
  /// they are added — put more specific translators first.
  void addTranslator(std::unique_ptr<TypeTranslator> Translator);

  /// Full resolution: tries each translator in order, then falls back
  /// to name-based lookup. Returns nullopt if the type cannot be resolved.
  std::optional<TypeResolution> resolve(clang::QualType Type) const;

  /// Get all registered type names (for debugging/testing).
  std::vector<std::string> registeredTypeNames() const;

private:
  struct RegisteredType {
    CType cType;
    bool isEnum = false;
  };

  std::map<std::string, RegisteredType> types_;
  std::vector<std::unique_ptr<TypeTranslator>> translators_;
};

// ---- Built-in translators ----

/// Handles builtin types: int, float, bool, uint32_t, etc.
/// Also normalizes std::uint32_t → uint32_t.
class BuiltinTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Handles enum types. Produces CType::Enum and sets the isEnum flag.
class EnumTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Handles pointer types: unwraps the pointer, resolves the inner type
/// through the registry, and wraps the result in CType::Pointer.
class PointerTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Handles lvalue reference types: strips the reference, resolves the
/// inner type, and converts to a pointer.
class ReferenceTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Handles container types: std::span<T>, std::vector<T>,
/// std::string, std::string_view. Sets the isContainer flag
/// so callers know to use callback/split-param handling.
class ContainerTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Handles record types (classes/structs) by looking them up in the
/// registry's known types. If found, maps to the registered C type.
class RecordTranslator : public TypeTranslator {
public:
  bool canTranslate(clang::QualType Type,
                    const TypeRegistry &Registry) const override;
  TypeResolution translate(clang::QualType Type,
                           const TypeRegistry &Registry) const override;
};

/// Create a TypeRegistry with all the standard translators pre-registered
/// in the correct order.
std::unique_ptr<TypeRegistry> createDefaultRegistry();

} // namespace hush

#endif // HUSH_EXPORT_TYPE_REGISTRY_H
