//===-- CBindingIR.h - Intermediate representation for C bindings ---------===//
//
// Fully resolved, output-ready representation of C bindings.
// All type decisions are finalized here — no C++ names, no string hacks.
// The emitter reads this IR and produces text without further logic.
//
//===----------------------------------------------------------------------===//

#ifndef HUSH_EXPORT_CBINDING_IR_H
#define HUSH_EXPORT_CBINDING_IR_H

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <cassert>

namespace hush {

/// Represents a fully resolved C-compatible type.
/// Compositional: pointers/const wrap an inner CType rather than
/// doing string manipulation.
struct CType {
  enum Kind {
    Builtin,       // int, float, bool, uint32_t, etc.
    Struct,        // A typedef'd struct (e.g., Vector3, MyClass)
    OpaqueHandle,  // Forward-declared opaque struct (typedef struct Foo Foo;)
    Enum,          // A C enum or typedef'd integer
    Pointer,       // T* — wraps an inner CType
    Void,          // void (for return types)
    FuncPointer,   // Function pointer field type (stored as raw string)
  };

  Kind kind;
  std::string name; // The C-side name (e.g., "Vector3", "uint32_t", "void")
  bool isConst = false;

  // For Kind::Pointer — the pointed-to type
  std::shared_ptr<CType> inner;

  // For Kind::FuncPointer — the raw declaration string
  // (e.g., "void (*)(int*, size_t, void*)")
  std::string funcPointerDecl;

  static CType makeBuiltin(const std::string &name) {
    return CType{Builtin, name, false, nullptr, {}};
  }

  static CType makeVoid() {
    return CType{Void, "void", false, nullptr, {}};
  }

  static CType makeStruct(const std::string &name) {
    return CType{Struct, name, false, nullptr, {}};
  }

  static CType makeOpaqueHandle(const std::string &name) {
    return CType{OpaqueHandle, name, false, nullptr, {}};
  }

  static CType makeEnum(const std::string &name) {
    return CType{Enum, name, false, nullptr, {}};
  }

  static CType makePointer(CType Pointee, bool IsConst = false) {
    auto Inner = std::make_shared<CType>(std::move(Pointee));
    return CType{Pointer, "", IsConst, std::move(Inner), {}};
  }

  static CType makeFuncPointer(const std::string &decl) {
    return CType{FuncPointer, "", false, nullptr, decl};
  }

  /// Format this type as a C declaration string.
  /// For most types this is just `name`, for pointers it recurses.
  std::string toString() const {
    switch (kind) {
    case Void:
    case Builtin:
    case Struct:
    case OpaqueHandle:
    case Enum:
      return (isConst ? "const " : "") + name;
    case Pointer:
      assert(inner && "Pointer CType must have inner type");
      return inner->toString() + " *";
    case FuncPointer:
      return funcPointerDecl;
    }
    return name;
  }

  /// Format as a parameter declaration: `type name`.
  /// For function pointers, the name is embedded in the declaration.
  std::string toParamDecl(const std::string &ParamName) const {
    if (kind == FuncPointer) {
      // Function pointers embed the name, caller handles this
      return funcPointerDecl;
    }
    return toString() + " " + ParamName;
  }
};

// ---- Struct IR ----

struct CField {
  std::string name;
  CType type;

  // For hidden/private fields: opaque padding instead of real type
  bool isOpaque = false;
  size_t opaqueSize = 0;  // in bytes
  size_t opaqueAlign = 0; // in bytes

  // For function pointer fields: the full decl string with name embedded
  // e.g., "void (*onClick)(int, float)"
  std::string funcPointerDeclWithName;
};

struct CStruct {
  std::string name;         // C-side name (e.g., "Vector3")
  std::string cppName;      // Original C++ name (e.g., "glm::vec3")
  std::vector<CField> fields;
  bool isOpaque = false;    // Opaque handle (no fields exposed)
  bool needsDestructor = false;

  // For destructor generation — the unqualified C++ class name
  // e.g., for "Hush::Rendering::Mesh" this is "Mesh"
  std::string cppUnqualifiedName;
};

// ---- Enum IR ----

struct CEnumValue {
  std::string name;
  int64_t value;
};

struct CEnumDef {
  std::string name; // C-side name

  // If true, emit as `typedef enum { ... } Name;`
  // If false, emit as `typedef underlying_type Name;` + #define constants
  bool isPlainEnum = false;

  // For scoped enums: the underlying integer type (e.g., "uint32_t")
  std::string underlyingType;

  std::vector<CEnumValue> values;
};

// ---- Function IR ----

/// How a C parameter maps to the C++ call site.
enum class PassMode {
  Direct,          // Pass through as-is
  Reinterpret,     // reinterpret_cast<cppType>(param)
  StaticCastEnum,  // static_cast<cppEnumType>(param)
  DerefReinterpret,// *reinterpret_cast<cppType>(param) (for ref params)
  SpanFromParts,   // Construct span/string_view from (data, size) pair
  ZStringFromParts,// Like SpanFromParts, but for NullTerminatedStringView.
                   // Its (data, size) constructor is private, so the call
                   // site must use promise_null_terminated().
};

struct CParam {
  std::string name;
  CType type;
  PassMode mode = PassMode::Direct;

  // For Reinterpret/StaticCast/DerefReinterpret: the C++ type to cast to
  std::string cppCastType;

  // For SpanFromParts: the C++ container type to construct
  // e.g., "std::span<const int>"
  std::string cppContainerType;
  // The real inner type for the reinterpret_cast on the data pointer
  // e.g., "const int"
  std::string cppInnerRealType;

  // If SpanFromParts, this param actually expands to two C params:
  //   T* <name>Data, size_t <name>Size
  // The `type` field holds the data pointer type; sizeName is derived.
  bool isSpanParts = false;
};

/// How the return value is converted from C++ to C.
enum class ReturnMode {
  Void,            // No return
  Direct,          // return result; (builtins)
  Callback,        // Invoke callback with (data, size, userData)
  PlacementNew,    // Move into aligned_storage to prevent destructor
  ReinterpretPtr,  // return reinterpret_cast<CType>(result);
  DerefReinterpret,// return *reinterpret_cast<CType*>(&result);
  StaticCastEnum,  // return static_cast<CEnumType>(result);
  ResultOutParams, // Result<T, E>: return bool status, T/E via out-params
};

struct CFunction {
  std::string name;     // C-side name
  std::string cppName;  // Fully qualified C++ name (e.g., "Hush::Scene::load")

  CType returnType;
  ReturnMode returnMode = ReturnMode::Void;

  // For ReturnMode::Callback — the inner element type for the callback
  // e.g., for std::vector<int> return, this is "int"
  std::string callbackInnerType;

  // For ReturnMode::PlacementNew/DerefReinterpret — the C++ return type
  std::string cppReturnType;

  // For ReturnMode::ResultOutParams: the C types of T and E.
  // resultValueCType is Void for Result<void, E>. No outValue parameter
  // is emitted then.
  CType resultValueCType = CType::makeVoid();
  CType resultErrorCType = CType::makeVoid();
  bool resultValueIsEnum = false;
  bool resultErrorIsEnum = false;

  std::vector<CParam> params;

  // For member functions: the self parameter
  bool isMemberFunction = false;
  std::string selfCType;  // C-side type of self (e.g., "MyClass")
  std::string selfCppType; // C++ type for reinterpret_cast (e.g., "Hush::MyClass")

  // For member functions: the C++ method name without class prefix
  // e.g., for "Hush::MyClass::doThing", this is "doThing"
  std::string cppMethodName;
};

// ---- Type alias IR ----

struct CTypeAlias {
  std::string name;        // C-side alias name (for registry tracking)
  std::string declaration; // Full typedef content between "typedef " and ";"
                           // e.g., "uint64_t Entity_EntityId"
                           // or "void (*CT_ComponentCtor)(void *, int32_t, const void *)"
};

// ---- Top-level IR ----

struct CBindingIR {
  std::vector<CTypeAlias> typeAliases;
  std::vector<CEnumDef> enums;
  std::vector<CStruct> structs;
  std::vector<CFunction> functions;
};

} // namespace hush

#endif // HUSH_EXPORT_CBINDING_IR_H
