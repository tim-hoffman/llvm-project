//===- OpCAPIGen.cpp - MLIR operation C API generator ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// OpCAPIGen uses the description of operations to generate C API for the ops.
//
//===----------------------------------------------------------------------===//

#include "mlir/TableGen/GenInfo.h"
#include "mlir/TableGen/Operator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <string>

using namespace mlir;
using namespace mlir::tblgen;

static llvm::cl::OptionCategory
    opGenCat("Options for -gen-op-capi-header and -gen-op-capi-impl");

static llvm::cl::opt<std::string>
    dialectName("dialect-name",
                llvm::cl::desc("The dialect name to use for this group of ops. "
                               "The form will be mlir<Dialect><Op><Accessor>, "
                               "e.g., mlirMyDialectAddOpGetLhs. The dialect "
                               "name helps avoid conflicts."),
                llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genCreate("gen-create",
              llvm::cl::desc("Generate operation create functions"),
              llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genOperandGetters("gen-operand-getters",
                      llvm::cl::desc("Generate operand getters"),
                      llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genOperandSetters("gen-operand-setters",
                      llvm::cl::desc("Generate operand setters"),
                      llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genAttributeGetters("gen-attribute-getters",
                        llvm::cl::desc("Generate attribute getters"),
                        llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genAttributeSetters("gen-attribute-setters",
                        llvm::cl::desc("Generate attribute setters"),
                        llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genResultGetters("gen-result-getters",
                     llvm::cl::desc("Generate result getters"),
                     llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genRegionGetters("gen-region-getters",
                     llvm::cl::desc("Generate region getters"),
                     llvm::cl::init(true), llvm::cl::cat(opGenCat));

static llvm::cl::opt<bool>
    genExtraClassMethods("gen-extra-class-methods",
                         llvm::cl::desc("Generate C API wrappers for methods "
                                        "in extraClassDeclaration"),
                         llvm::cl::init(true), llvm::cl::cat(opGenCat));

const char *const opDecl = R"(
/* Create {0} Operation. */
MLIR_CAPI_EXPORTED MlirOperation mlirCreate{0}{1}(MlirContext ctx, MlirLocation location{2});
)";

const char *const operandGetterDecl = R"(
/* Get {2} operand from {0} Operation. */
MLIR_CAPI_EXPORTED MlirValue mlir{0}{1}Get{2}(MlirOperation op);
)";

const char *const operandSetterDecl = R"(
/* Set {2} operand of {0} Operation. */
MLIR_CAPI_EXPORTED void mlir{0}{1}Set{2}(MlirOperation op, MlirValue value);
)";

const char *const variadicOperandCountGetterDecl = R"(
/* Get number of {2} operands in {0} Operation. */
MLIR_CAPI_EXPORTED intptr_t mlir{0}{1}Get{2}Count(MlirOperation op);
)";

const char *const variadicOperandIndexedGetterDecl = R"(
/* Get {2} operand at index from {0} Operation. */
MLIR_CAPI_EXPORTED MlirValue mlir{0}{1}Get{2}(MlirOperation op, intptr_t index);
)";

const char *const variadicOperandSetterDecl = R"(
/* Set {2} operands of {0} Operation. */
MLIR_CAPI_EXPORTED void mlir{0}{1}Set{2}(MlirOperation op, intptr_t count, MlirValue const *values);
)";

const char *const attributeGetterDecl = R"(
/* Get {2} attribute from {0} Operation. */
MLIR_CAPI_EXPORTED MlirAttribute mlir{0}{1}Get{2}(MlirOperation op);
)";

const char *const attributeSetterDecl = R"(
/* Set {2} attribute of {0} Operation. */
MLIR_CAPI_EXPORTED void mlir{0}{1}Set{2}(MlirOperation op, MlirAttribute attr);
)";

const char *const resultGetterDecl = R"(
/* Get {2} result from {0} Operation. */
MLIR_CAPI_EXPORTED MlirValue mlir{0}{1}Get{2}(MlirOperation op);
)";

const char *const variadicResultCountGetterDecl = R"(
/* Get number of {2} results in {0} Operation. */
MLIR_CAPI_EXPORTED intptr_t mlir{0}{1}Get{2}Count(MlirOperation op);
)";

const char *const variadicResultIndexedGetterDecl = R"(
/* Get {2} result at index from {0} Operation. */
MLIR_CAPI_EXPORTED MlirValue mlir{0}{1}Get{2}(MlirOperation op, intptr_t index);
)";

const char *const regionGetterDecl = R"(
/* Get {2} region from {0} Operation. */
MLIR_CAPI_EXPORTED MlirRegion mlir{0}{1}Get{2}(MlirOperation op);
)";

const char *const variadicRegionCountGetterDecl = R"(
/* Get number of {2} regions in {0} Operation. */
MLIR_CAPI_EXPORTED intptr_t mlir{0}{1}Get{2}Count(MlirOperation op);
)";

const char *const variadicRegionIndexedGetterDecl = R"(
/* Get {2} region at index from {0} Operation. */
MLIR_CAPI_EXPORTED MlirRegion mlir{0}{1}Get{2}(MlirOperation op, intptr_t index);
)";

const char *const extraMethodDecl = R"(
/* {0} */
MLIR_CAPI_EXPORTED {1} mlir{2}{3}{4}(MlirOperation op);
)";

const char *const fileHeader = R"(
/* Autogenerated by mlir-tblgen; don't manually edit. */

#include "mlir-c/IR.h"

#ifdef __cplusplus
extern "C" {
#endif
)";

const char *const fileFooter = R"(
#ifdef __cplusplus
}
#endif
)";

/// Convert underscore-separated name to camelCase (e.g., "no_inline" ->
/// "NoInline")
static std::string toCamelCase(StringRef str) {
  if (str.empty())
    return "";

  std::string result;
  bool capitalizeNext = true;

  for (char c : str) {
    if (c == '_') {
      capitalizeNext = true;
    } else {
      if (capitalizeNext && c >= 'a' && c <= 'z') {
        result += c - 'a' + 'A';
        capitalizeNext = false;
      } else {
        result += c;
        capitalizeNext = false;
      }
    }
  }

  return result;
}

/// Structure to represent a parsed method signature from extraClassDeclaration
struct ExtraMethod {
  std::string returnType;
  std::string methodName;
  std::string documentation;
  bool isConst = false;
  bool hasParameters = false;
};

/// Extract documentation comment preceding a method declaration
static std::string extractDocumentation(StringRef extraDecl, size_t methodPos) {
  std::string doc;

  // Search backwards to find the start of this declaration
  // (which might include keywords like inline/static/virtual before the return
  // type)
  size_t searchStart = methodPos;

  // Go back to find the previous method/declaration end (;, }, or start of
  // file)
  while (searchStart > 0) {
    searchStart--;
    char c = extraDecl[searchStart];
    if (c == ';' || c == '}') {
      // Found the end of the previous declaration
      searchStart++; // Move past it
      break;
    }
  }

  // Skip any whitespace/newlines at the start
  while (searchStart < methodPos &&
         (extraDecl[searchStart] == ' ' || extraDecl[searchStart] == '\t' ||
          extraDecl[searchStart] == '\n' || extraDecl[searchStart] == '\r'))
    searchStart++;

  // Look for /// or /* */ style comments from searchStart to methodPos
  size_t searchPos = searchStart;

  while (searchPos < methodPos) {
    // Skip whitespace
    while (searchPos < methodPos &&
           (extraDecl[searchPos] == ' ' || extraDecl[searchPos] == '\t'))
      searchPos++;

    // Check for /// comment
    if (searchPos + 2 < methodPos && extraDecl.substr(searchPos, 3) == "///") {
      size_t lineEnd = extraDecl.find('\n', searchPos);
      if (lineEnd == StringRef::npos)
        lineEnd = methodPos;

      // Extract the comment content (skip "///" and leading spaces)
      size_t contentStart = searchPos + 3;
      while (contentStart < lineEnd && extraDecl[contentStart] == ' ')
        contentStart++;

      if (!doc.empty())
        doc += " ";
      doc += extraDecl.substr(contentStart, lineEnd - contentStart).str();

      searchPos = lineEnd + 1;
      continue;
    }

    // Check for /* */ comment
    if (searchPos + 1 < methodPos && extraDecl[searchPos] == '/' &&
        extraDecl[searchPos + 1] == '*') {
      size_t commentStart = searchPos;
      searchPos = extraDecl.find("*/", searchPos + 2);
      if (searchPos != StringRef::npos) {
        // Extract content between /* and */
        size_t contentStart = commentStart + 2;
        size_t contentEnd = searchPos;

        // Clean up the comment content
        StringRef content =
            extraDecl.substr(contentStart, contentEnd - contentStart);
        content = content.trim();

        if (!doc.empty())
          doc += " ";
        doc += content.str();

        searchPos += 2;
        continue;
      }
    }

    // If we hit something that's not whitespace or comment, stop looking
    if (searchPos < methodPos && extraDecl[searchPos] != '\n')
      break;

    if (searchPos < methodPos && extraDecl[searchPos] == '\n')
      searchPos++;
  }

  return doc;
}

/// Parse method declarations from extraClassDeclaration
static std::vector<ExtraMethod> parseExtraMethods(StringRef extraDecl) {
  std::vector<ExtraMethod> methods;

  if (extraDecl.empty())
    return methods;

  // Simple heuristic: look for lines that appear to be method declarations
  // Pattern: <return type> <methodName>(<params>) [const] [;|{]

  size_t pos = 0;
  while (pos < extraDecl.size()) {
    // Skip to next potential method
    // Look for an identifier followed by '('
    size_t parenPos = extraDecl.find('(', pos);
    if (parenPos == StringRef::npos)
      break;

    // Find the start of the method name (work backwards from '(')
    size_t nameEnd = parenPos;
    while (nameEnd > pos && std::isspace(extraDecl[nameEnd - 1]))
      nameEnd--;

    if (nameEnd <= pos) {
      pos = parenPos + 1;
      continue;
    }

    size_t nameStart = nameEnd;
    while (nameStart > pos && (std::isalnum(extraDecl[nameStart - 1]) ||
                               extraDecl[nameStart - 1] == '_'))
      nameStart--;

    if (nameStart >= nameEnd) {
      pos = parenPos + 1;
      continue;
    }

    std::string methodName =
        extraDecl.substr(nameStart, nameEnd - nameStart).str();

    // Skip if it looks like a keyword (if, for, while, etc.)
    if (methodName == "if" || methodName == "for" || methodName == "while" ||
        methodName == "switch" || methodName == "return") {
      pos = parenPos + 1;
      continue;
    }

    // Find the return type (everything before the method name)
    size_t returnTypeEnd = nameStart;
    while (returnTypeEnd > pos && std::isspace(extraDecl[returnTypeEnd - 1]))
      returnTypeEnd--;

    // Look backwards to find the start of the return type
    // It should start after a newline, semicolon, or brace
    size_t returnTypeStart = pos;
    for (size_t i = returnTypeEnd; i > pos; i--) {
      if (extraDecl[i - 1] == '\n' || extraDecl[i - 1] == ';' ||
          extraDecl[i - 1] == '{' || extraDecl[i - 1] == '}') {
        returnTypeStart = i;
        break;
      }
    }

    // Skip whitespace at the start
    while (returnTypeStart < returnTypeEnd &&
           std::isspace(extraDecl[returnTypeStart]))
      returnTypeStart++;

    if (returnTypeStart >= returnTypeEnd) {
      pos = parenPos + 1;
      continue;
    }

    std::string returnType =
        extraDecl.substr(returnTypeStart, returnTypeEnd - returnTypeStart)
            .str();

    // Trim whitespace from return type
    size_t rtStart = 0;
    while (rtStart < returnType.size() && std::isspace(returnType[rtStart]))
      rtStart++;
    size_t rtEnd = returnType.size();
    while (rtEnd > rtStart && std::isspace(returnType[rtEnd - 1]))
      rtEnd--;
    returnType = returnType.substr(rtStart, rtEnd - rtStart);

    // Skip inline/static/virtual keywords
    if (returnType.find("inline ") == 0)
      returnType = returnType.substr(7);
    if (returnType.find("static ") == 0)
      returnType = returnType.substr(7);
    if (returnType.find("virtual ") == 0)
      returnType = returnType.substr(8);

    // Trim again
    rtStart = 0;
    while (rtStart < returnType.size() && std::isspace(returnType[rtStart]))
      rtStart++;
    returnType = returnType.substr(rtStart);

    // Find the end of the method signature
    size_t signatureEnd = extraDecl.find(';', parenPos);
    if (signatureEnd == StringRef::npos)
      signatureEnd = extraDecl.find('{', parenPos);
    if (signatureEnd == StringRef::npos) {
      pos = parenPos + 1;
      continue;
    }

    // Check if method is const
    StringRef signature = extraDecl.substr(parenPos, signatureEnd - parenPos);
    bool isConst = signature.contains("const");

    // Check if method has parameters
    size_t closeParenPos = extraDecl.find(')', parenPos);
    bool hasParameters = false;
    if (closeParenPos != StringRef::npos && closeParenPos < signatureEnd) {
      StringRef params =
          extraDecl.substr(parenPos + 1, closeParenPos - parenPos - 1);
      params = params.trim();
      hasParameters = !params.empty() && params != "void";
    }

    // Extract documentation
    std::string documentation =
        extractDocumentation(extraDecl, returnTypeStart);

    // Create the method struct
    ExtraMethod method;
    method.returnType = returnType;
    method.methodName = methodName;
    method.documentation = documentation;
    method.isConst = isConst;
    method.hasParameters = hasParameters;

    methods.push_back(method);

    pos = signatureEnd + 1;
  }

  return methods;
}

/// Convert C++ return type to MLIR C API type
static std::string cppTypeToCapiType(StringRef cppType) {
  cppType = cppType.trim();

  // Handle basic types
  if (cppType == "bool")
    return "bool";
  if (cppType == "int" || cppType == "int32_t" || cppType == "int64_t" ||
      cppType == "intptr_t" || cppType == "size_t" || cppType == "unsigned" ||
      cppType == "uint32_t" || cppType == "uint64_t")
    return cppType.str();
  if (cppType == "void")
    return "void";

  // Handle pointer types
  if (cppType.ends_with(" *") || cppType.ends_with("*")) {
    StringRef baseType = cppType;
    // Remove the pointer
    size_t starPos = cppType.rfind('*');
    if (starPos != StringRef::npos) {
      baseType = cppType.substr(0, starPos).trim();
    }

    // Check if it's a region pointer
    if (baseType.ends_with("Region") || baseType.ends_with("::mlir::Region") ||
        baseType == "::mlir::Region" || baseType == "mlir::Region")
      return "MlirRegion";

    // Check if it's an operation pointer
    if (baseType.ends_with("Operation") || baseType == "::mlir::Operation" ||
        baseType == "mlir::Operation")
      return "MlirOperation";
  }

  // Handle ArrayRef and other template types - these are not directly supported
  // For now, skip these (they would need special handling)
  if (cppType.contains("ArrayRef") || cppType.contains("SmallVector") ||
      cppType.contains("iterator_range"))
    return cppType.str(); // Return as-is, will be skipped

  // Check for mlir::Value
  if (cppType == "::mlir::Value" || cppType == "mlir::Value" ||
      cppType == "Value")
    return "MlirValue";

  // Check for mlir::Type or any type that inherits from it
  // This includes custom dialect types like ::llzk::component::StructType
  if (cppType == "::mlir::Type" || cppType == "mlir::Type" ||
      cppType == "Type" || cppType.ends_with("Type"))
    return "MlirType";

  // Check for mlir::Attribute or any attribute subclass
  if (cppType == "::mlir::Attribute" || cppType == "mlir::Attribute" ||
      cppType == "Attribute" || cppType.ends_with("Attr"))
    return "MlirAttribute";

  // Check for mlir::Region
  if (cppType == "::mlir::Region" || cppType == "mlir::Region" ||
      cppType == "Region")
    return "MlirRegion";

  // Check for mlir::Block
  if (cppType == "::mlir::Block" || cppType == "mlir::Block" ||
      cppType == "Block")
    return "MlirBlock";

  // Check for mlir::Operation
  if (cppType == "::mlir::Operation" || cppType == "mlir::Operation" ||
      cppType == "Operation")
    return "MlirOperation";

  // Default: return as-is (may need manual wrapping)
  return cppType.str();
}

/// Determine the wrapping code needed for a return value
static std::string getReturnWrapCode(StringRef capiType, StringRef cppType) {
  capiType = capiType.trim();
  cppType = cppType.trim();

  if (capiType == "bool" || capiType == "int" || capiType == "int32_t" ||
      capiType == "int64_t" || capiType == "intptr_t" || capiType == "size_t" ||
      capiType == "void")
    return ""; // No wrapping needed

  if (capiType == "MlirValue")
    return "wrap";
  if (capiType == "MlirType")
    return "wrap";
  if (capiType == "MlirAttribute")
    return "wrap";
  if (capiType == "MlirRegion")
    return "wrap";
  if (capiType == "MlirBlock")
    return "wrap";
  if (capiType == "MlirOperation")
    return "wrap";

  return ""; // Unknown, no wrapping
}

/// Generate C API parameter list from operation arguments
static std::string generateCAPIParams(const Operator &op) {
  std::string params;

  // Add operands
  for (int i = 0, e = op.getNumOperands(); i < e; ++i) {
    const auto &operand = op.getOperand(i);
    if (operand.isVariadic()) {
      params += llvm::formatv(", intptr_t {0}Size, MlirValue const *{0}",
                              operand.name)
                    .str();
    } else {
      params += llvm::formatv(", MlirValue {0}", operand.name).str();
    }
  }

  // Add attributes
  for (const auto &namedAttr : op.getAttributes()) {
    params += llvm::formatv(", MlirAttribute {0}", namedAttr.name).str();
  }

  // Add result types if not inferred
  if (!op.allResultTypesKnown()) {
    for (int i = 0, e = op.getNumResults(); i < e; ++i) {
      const auto &result = op.getResult(i);
      std::string resultName = result.name.empty()
                                   ? llvm::formatv("result{0}", i).str()
                                   : result.name.str();
      if (result.isVariadic()) {
        params += llvm::formatv(", intptr_t {0}Size, MlirType const *{0}Types",
                                resultName)
                      .str();
      } else {
        params += llvm::formatv(", MlirType {0}Type", resultName).str();
      }
    }
  }

  // Add regions
  for (int i = 0, e = op.getNumRegions(); i < e; ++i) {
    const auto &region = op.getRegion(i);
    if (region.isVariadic()) {
      params += llvm::formatv(", intptr_t {0}Size, MlirRegion const *{0}",
                              region.name.empty()
                                  ? llvm::formatv("region{0}", i).str()
                                  : region.name)
                    .str();
    } else {
      params += llvm::formatv(", MlirRegion {0}",
                              region.name.empty()
                                  ? llvm::formatv("region{0}", i).str()
                                  : region.name)
                    .str();
    }
  }

  return params;
}

/// Emit C API header
static bool emitOpCAPIHeader(const llvm::RecordKeeper &records,
                             raw_ostream &os) {
  os << fileHeader;

  for (const auto *def : records.getAllDerivedDefinitions("Op")) {
    Operator op(def);
    StringRef opName = op.getCppClassName();
    std::string params = generateCAPIParams(op);

    // Generate create function
    if (genCreate)
      os << llvm::formatv(opDecl, dialectName, opName, params);

    // Generate operand getters and setters
    for (int i = 0, e = op.getNumOperands(); i < e; ++i) {
      const auto &operand = op.getOperand(i);
      std::string capitalizedName = toCamelCase(operand.name);
      if (!operand.isVariadic()) {
        if (genOperandGetters)
          os << llvm::formatv(operandGetterDecl, dialectName, opName,
                              capitalizedName);
        if (genOperandSetters)
          os << llvm::formatv(operandSetterDecl, dialectName, opName,
                              capitalizedName);
      } else {
        if (genOperandGetters) {
          os << llvm::formatv(variadicOperandCountGetterDecl, dialectName,
                              opName, capitalizedName);
          os << llvm::formatv(variadicOperandIndexedGetterDecl, dialectName,
                              opName, capitalizedName);
        }
        if (genOperandSetters) {
          os << llvm::formatv(variadicOperandSetterDecl, dialectName, opName,
                              capitalizedName);
        }
      }
    }

    // Generate attribute getters and setters
    for (const auto &namedAttr : op.getAttributes()) {
      std::string capitalizedName = toCamelCase(namedAttr.name);
      if (genAttributeGetters)
        os << llvm::formatv(attributeGetterDecl, dialectName, opName,
                            capitalizedName);
      if (genAttributeSetters)
        os << llvm::formatv(attributeSetterDecl, dialectName, opName,
                            capitalizedName);
    }

    // Generate result getters
    if (genResultGetters) {
      for (int i = 0, e = op.getNumResults(); i < e; ++i) {
        const auto &result = op.getResult(i);
        std::string resultName = result.name.empty()
                                     ? llvm::formatv("Result{0}", i).str()
                                     : result.name.str();
        std::string capitalizedName = toCamelCase(resultName);
        if (!result.isVariadic()) {
          os << llvm::formatv(resultGetterDecl, dialectName, opName,
                              capitalizedName);
        } else {
          os << llvm::formatv(variadicResultCountGetterDecl, dialectName,
                              opName, capitalizedName);
          os << llvm::formatv(variadicResultIndexedGetterDecl, dialectName,
                              opName, capitalizedName);
        }
      }
    }

    // Generate region getters
    if (genRegionGetters) {
      for (int i = 0, e = op.getNumRegions(); i < e; ++i) {
        const auto &region = op.getRegion(i);
        std::string regionName = region.name.empty()
                                     ? llvm::formatv("Region{0}", i).str()
                                     : region.name.str();
        std::string capitalizedName = toCamelCase(regionName);
        if (!region.isVariadic()) {
          os << llvm::formatv(regionGetterDecl, dialectName, opName,
                              capitalizedName);
        } else {
          os << llvm::formatv(variadicRegionCountGetterDecl, dialectName,
                              opName, capitalizedName);
          os << llvm::formatv(variadicRegionIndexedGetterDecl, dialectName,
                              opName, capitalizedName);
        }
      }
    }

    // Generate extra class method wrappers
    if (genExtraClassMethods) {
      StringRef extraDecl = op.getExtraClassDeclaration();
      if (!extraDecl.empty()) {
        std::vector<ExtraMethod> methods = parseExtraMethods(extraDecl);
        for (const auto &method : methods) {
          // Skip methods with parameters (not supported yet)
          if (method.hasParameters)
            continue;

          // Convert return type to C API type
          std::string capiReturnType = cppTypeToCapiType(method.returnType);

          // Skip if the return type couldn't be converted (e.g., ArrayRef,
          // SmallVector) Check if conversion failed by seeing if the type is
          // unchanged and not a known primitive
          bool isKnownPrimitive =
              (capiReturnType == "bool" || capiReturnType == "void" ||
               capiReturnType == "size_t" || capiReturnType.find("int") == 0);
          if (capiReturnType == method.returnType && !isKnownPrimitive)
            continue;

          std::string capitalizedMethodName = toCamelCase(method.methodName);

          // Generate declaration
          std::string docComment = method.documentation.empty()
                                       ? method.methodName
                                       : method.documentation;
          os << llvm::formatv(extraMethodDecl, docComment, capiReturnType,
                              dialectName, opName, capitalizedMethodName);
        }
      }
    }
  }

  os << fileFooter;
  return false;
}

const char *const opCreateDef = R"(
MlirOperation mlirCreate{0}{1}(MlirContext ctx, MlirLocation location{2}) {{
  MlirOperationState state = mlirOperationStateGet(mlirStringRefCreateFromCString("{3}"), location);
{4}
  return mlirOperationCreate(&state);
}
)";

const char *const operandGetterDef = R"(
MlirValue mlir{0}{1}Get{2}(MlirOperation op) {{
  return mlirOperationGetOperand(op, {3});
}
)";

const char *const operandSetterDef = R"(
void mlir{0}{1}Set{2}(MlirOperation op, MlirValue value) {{
  mlirOperationSetOperand(op, {3}, value);
}
)";

const char *const variadicOperandCountGetterDef = R"(
intptr_t mlir{0}{1}Get{2}Count(MlirOperation op) {{
  return {3} - {4};
}
)";

const char *const variadicOperandIndexedGetterDef = R"(
MlirValue mlir{0}{1}Get{2}(MlirOperation op, intptr_t index) {{
  return mlirOperationGetOperand(op, {3} + index);
}
)";

const char *const variadicOperandSetterDef = R"(
void mlir{0}{1}Set{2}(MlirOperation op, intptr_t count, MlirValue const *values) {{
  intptr_t numOperands = mlirOperationGetNumOperands(op);
  intptr_t startIdx = {3};
  intptr_t oldCount = numOperands - startIdx;
  intptr_t newNumOperands = numOperands - oldCount + count;
  MlirValue newOperands[newNumOperands];
  
  // Copy operands before this variadic group
  for (intptr_t i = 0; i < startIdx; ++i) {{
    newOperands[i] = mlirOperationGetOperand(op, i);
  }
  
  // Copy new variadic operands
  for (intptr_t i = 0; i < count; ++i) {{
    newOperands[startIdx + i] = values[i];
  }
  
  // Copy operands after this variadic group
  for (intptr_t i = startIdx + oldCount; i < numOperands; ++i) {{
    newOperands[i - oldCount + count] = mlirOperationGetOperand(op, i);
  }
  
  mlirOperationSetOperands(op, newNumOperands, newOperands);
}
)";

const char *const attributeGetterDef = R"(
MlirAttribute mlir{0}{1}Get{2}(MlirOperation op) {{
  return mlirOperationGetAttributeByName(op, mlirStringRefCreateFromCString("{3}"));
}
)";

const char *const attributeSetterDef = R"(
void mlir{0}{1}Set{2}(MlirOperation op, MlirAttribute attr) {{
  mlirOperationSetAttributeByName(op, mlirStringRefCreateFromCString("{3}"), attr);
}
)";

const char *const resultGetterDef = R"(
MlirValue mlir{0}{1}Get{2}(MlirOperation op) {{
  return mlirOperationGetResult(op, {3});
}
)";

const char *const variadicResultCountGetterDef = R"(
intptr_t mlir{0}{1}Get{2}Count(MlirOperation op) {{
  return {3} - {4};
}
)";

const char *const variadicResultIndexedGetterDef = R"(
MlirValue mlir{0}{1}Get{2}(MlirOperation op, intptr_t index) {{
  return mlirOperationGetResult(op, {3} + index);
}
)";

const char *const regionGetterDef = R"(
MlirRegion mlir{0}{1}Get{2}(MlirOperation op) {{
  return mlirOperationGetRegion(op, {3});
}
)";

const char *const variadicRegionCountGetterDef = R"(
intptr_t mlir{0}{1}Get{2}Count(MlirOperation op) {{
  return {3} - {4};
}
)";

const char *const variadicRegionIndexedGetterDef = R"(
MlirRegion mlir{0}{1}Get{2}(MlirOperation op, intptr_t index) {{
  return mlirOperationGetRegion(op, {3} + index);
}
)";

const char *const extraMethodDef = R"(
{0} mlir{1}{2}{3}(MlirOperation op) {{
  {4}mlir::unwrap_cast<{5}>(op).{6}(){7};
}
)";

/// Generate C API parameter assignments for operation creation
static std::string generateCAPIAssignments(const Operator &op) {
  std::string assignments;

  // Add operands
  for (int i = 0, e = op.getNumOperands(); i < e; ++i) {
    const auto &operand = op.getOperand(i);
    if (operand.isVariadic()) {
      assignments +=
          llvm::formatv(
              "  mlirOperationStateAddOperands(&state, {0}Size, {0});\n",
              operand.name)
              .str();
    } else {
      assignments +=
          llvm::formatv("  mlirOperationStateAddOperands(&state, 1, &{0});\n",
                        operand.name)
              .str();
    }
  }

  // Add attributes
  auto attributes = op.getAttributes();
  if (attributes.begin() != attributes.end()) {
    assignments += "  MlirNamedAttribute attributes[] = {\n";
    for (const auto &namedAttr : attributes) {
      assignments +=
          "    { mlirIdentifierGet(ctx, mlirStringRefCreateFromCString(\"" +
          namedAttr.name.str() + "\")), " + namedAttr.name.str() + " },\n";
    }
    assignments += "  };\n";
    assignments +=
        llvm::formatv(
            "  mlirOperationStateAddAttributes(&state, {0}, attributes);\n",
            op.getNumAttributes())
            .str();
  }

  // Add result types if not inferred
  if (!op.allResultTypesKnown()) {
    for (int i = 0, e = op.getNumResults(); i < e; ++i) {
      const auto &result = op.getResult(i);
      std::string resultName = result.name.empty()
                                   ? llvm::formatv("result{0}", i).str()
                                   : result.name.str();
      if (result.isVariadic()) {
        assignments +=
            llvm::formatv(
                "  mlirOperationStateAddResults(&state, {0}Size, {0}Types);\n",
                resultName)
                .str();
      } else {
        assignments +=
            llvm::formatv(
                "  mlirOperationStateAddResults(&state, 1, &{0}Type);\n",
                resultName)
                .str();
      }
    }
  } else {
    assignments += "  mlirOperationStateEnableResultTypeInference(&state);\n";
  }

  // Add regions
  for (int i = 0, e = op.getNumRegions(); i < e; ++i) {
    const auto &region = op.getRegion(i);
    std::string regionName = region.name.empty()
                                 ? llvm::formatv("region{0}", i).str()
                                 : region.name.str();
    if (region.isVariadic()) {
      assignments +=
          llvm::formatv(
              "  mlirOperationStateAddOwnedRegions(&state, {0}Size, {0});\n",
              regionName)
              .str();
    } else {
      assignments +=
          llvm::formatv(
              "  mlirOperationStateAddOwnedRegions(&state, 1, &{0});\n",
              regionName)
              .str();
    }
  }

  return assignments;
}

/// Emit C API implementation
static bool emitOpCAPIImpl(const llvm::RecordKeeper &records, raw_ostream &os) {
  os << "/* Autogenerated by mlir-tblgen; don't manually edit. */\n";

  for (const auto *def : records.getAllDerivedDefinitions("Op")) {
    Operator op(def);
    StringRef opName = op.getCppClassName();
    std::string params = generateCAPIParams(op);
    std::string assignments = generateCAPIAssignments(op);
    std::string operationName = op.getOperationName();

    // Generate create function
    if (genCreate) {
      os << llvm::formatv(opCreateDef, dialectName, opName, params,
                          operationName, assignments);
    }

    // Generate operand getters and setters
    for (int i = 0, e = op.getNumOperands(); i < e; ++i) {
      const auto &operand = op.getOperand(i);
      std::string capitalizedName = toCamelCase(operand.name);
      if (!operand.isVariadic()) {
        if (genOperandGetters)
          os << llvm::formatv(operandGetterDef, dialectName, opName,
                              capitalizedName, i);
        if (genOperandSetters)
          os << llvm::formatv(operandSetterDef, dialectName, opName,
                              capitalizedName, i);
      } else {
        // Calculate the start index for this variadic operand
        int startIdx = i;

        if (genOperandGetters) {
          os << llvm::formatv(variadicOperandCountGetterDef, dialectName,
                              opName, capitalizedName,
                              "mlirOperationGetNumOperands(op)", startIdx);
          os << llvm::formatv(variadicOperandIndexedGetterDef, dialectName,
                              opName, capitalizedName, startIdx);
        }
        if (genOperandSetters) {
          os << llvm::formatv(variadicOperandSetterDef, dialectName, opName,
                              capitalizedName, startIdx);
        }
      }
    }

    // Generate attribute getters and setters
    for (const auto &namedAttr : op.getAttributes()) {
      std::string capitalizedName = toCamelCase(namedAttr.name);
      if (genAttributeGetters)
        os << llvm::formatv(attributeGetterDef, dialectName, opName,
                            capitalizedName, namedAttr.name);
      if (genAttributeSetters)
        os << llvm::formatv(attributeSetterDef, dialectName, opName,
                            capitalizedName, namedAttr.name);
    }

    // Generate result getters
    if (genResultGetters) {
      for (int i = 0, e = op.getNumResults(); i < e; ++i) {
        const auto &result = op.getResult(i);
        std::string resultName = result.name.empty()
                                     ? llvm::formatv("Result{0}", i).str()
                                     : result.name.str();
        std::string capitalizedName = toCamelCase(resultName);
        if (!result.isVariadic()) {
          os << llvm::formatv(resultGetterDef, dialectName, opName,
                              capitalizedName, i);
        } else {
          // Calculate the start index for this variadic result
          int startIdx = i;

          os << llvm::formatv(variadicResultCountGetterDef, dialectName, opName,
                              capitalizedName, "mlirOperationGetNumResults(op)",
                              startIdx);
          os << llvm::formatv(variadicResultIndexedGetterDef, dialectName,
                              opName, capitalizedName, startIdx);
        }
      }
    }

    // Generate region getters
    if (genRegionGetters) {
      for (int i = 0, e = op.getNumRegions(); i < e; ++i) {
        const auto &region = op.getRegion(i);
        std::string regionName = region.name.empty()
                                     ? llvm::formatv("Region{0}", i).str()
                                     : region.name.str();
        std::string capitalizedName = toCamelCase(regionName);
        if (!region.isVariadic()) {
          os << llvm::formatv(regionGetterDef, dialectName, opName,
                              capitalizedName, i);
        } else {
          // Calculate the start index for this variadic region
          int startIdx = i;

          os << llvm::formatv(variadicRegionCountGetterDef, dialectName, opName,
                              capitalizedName, "mlirOperationGetNumRegions(op)",
                              startIdx);
          os << llvm::formatv(variadicRegionIndexedGetterDef, dialectName,
                              opName, capitalizedName, startIdx);
        }
      }
    }

    // Generate extra class method implementations
    if (genExtraClassMethods) {
      StringRef extraDecl = op.getExtraClassDeclaration();
      if (!extraDecl.empty()) {
        std::vector<ExtraMethod> methods = parseExtraMethods(extraDecl);
        for (const auto &method : methods) {
          // Skip methods with parameters (not supported yet)
          if (method.hasParameters)
            continue;

          // Convert return type to C API type
          std::string capiReturnType = cppTypeToCapiType(method.returnType);

          // Skip if the return type couldn't be converted (e.g., ArrayRef,
          // SmallVector) Check if conversion failed by seeing if the type is
          // unchanged and not a known primitive
          bool isKnownPrimitive =
              (capiReturnType == "bool" || capiReturnType == "void" ||
               capiReturnType == "size_t" || capiReturnType.find("int") == 0);
          if (capiReturnType == method.returnType && !isKnownPrimitive)
            continue;

          std::string capitalizedMethodName = toCamelCase(method.methodName);
          std::string wrapCode =
              getReturnWrapCode(capiReturnType, method.returnType);

          // Build the return statement prefix and suffix
          std::string returnPrefix;
          std::string returnSuffix;

          if (capiReturnType == "void") {
            returnPrefix = "";
            returnSuffix = "";
          } else if (!wrapCode.empty()) {
            returnPrefix = "return " + wrapCode + "(";
            returnSuffix = ")";
          } else {
            returnPrefix = "return ";
            returnSuffix = "";
          }

          // Generate implementation
          os << llvm::formatv(extraMethodDef,
                              capiReturnType,        // {0}
                              dialectName,           // {1}
                              opName,                // {2}
                              capitalizedMethodName, // {3}
                              returnPrefix,          // {4}
                              opName,                // {5}
                              method.methodName,     // {6}
                              returnSuffix);         // {7}
        }
      }
    }
  }

  return false;
}

static mlir::GenRegistration genOpCAPIHeader("gen-op-capi-header",
                                             "Generate operation C API header",
                                             &emitOpCAPIHeader);

static mlir::GenRegistration
    genOpCAPIImpl("gen-op-capi-impl", "Generate operation C API implementation",
                  &emitOpCAPIImpl);
