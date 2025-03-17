
#include "FunctionParser.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/Basic/SourceManager.h>

void processSpanParameter(const clang::ParmVarDecl *D,
                          llvm::raw_ostream &OutHeaderFile) {
}

/// Exports function to the binding
///
/// This process is kind of complex as we need to check if it is a free function
/// or a member function. If it is a member function, we need to check if it is
/// a static member function or not.
///
/// For instance, the following declaration:
/// ```cpp
///   class [[Hush::export]] MyClass {
///     [[Hush::export]]
///     void MyFunction();
///   };
///  ```
///
///  The generated code should be:
///   ```cpp
///   #include "HushBindings.h" // For the exported class
///   #include "MyClass.h" // For the class definition
///
///   void Hush__MyClass__MyFunction(MyClass *self) {
///     MyClass* self = reinterpret_cast<MyClass*>(self);
///     self->MyFunction();
///   }
///   ```
///
///   For free functions it is simpler as we don't need to pass the `self`.
///
///  However, certain parameters are more complex to handle, such as `std::span`, `std::string_view` or `std::string`.
///  For instance, the following declaration:
///  ```cpp
///   [[Hush::export]]
///   void MyFunction(std::span<int> values, std::string_view name);
///  ```
///
///  The generated code should be:
///  ```cpp
///    void Hush__MyFunction(int *valuesData, int valuesSize, const char *nameData, int nameSize) {
///      std::span<int> values(valuesData, valuesSize);
///      std::string_view name(nameData, nameSize);
///      MyFunction(values, name);
///  }
///  ```
///
///  For the following function:
///  ```cpp
///   [[Hush::export]]
///   std::string MyFunction();
///   std::vector<int> MyFunction();
///   ```
///
///   The generated code should be:
///   ```cpp
///     void Hush__MyFunction(char *outBuffer, int outBufferSize) {
///       std::string result = MyFunction();
///       std::memcpy(outBuffer, result.data(), std::min(outBufferSize, result.size()));
///     }
///
///     void Hush__MyFunction(int *outBuffer, int outBufferSize) {
///       std::vector<int> result = MyFunction();
///       std::memcpy(outBuffer, result.data(), std::min(outBufferSize, result.size() * sizeof(int)));
///     }
///     ```
///
void processFunctionDecl(
    std::map<std::string, ExportedClassInfo> &ParsedClasses,
    const clang::HushExportAttr *HushExportAttr, const clang::FunctionDecl *D,
    llvm::raw_ostream &OutHeaderFile, llvm::raw_ostream &OutCppFile,
    llvm::raw_ostream &OutPrototypeFile) {

}