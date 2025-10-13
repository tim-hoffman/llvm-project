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
  }

  return false;
}

static mlir::GenRegistration genOpCAPIHeader("gen-op-capi-header",
                                             "Generate operation C API header",
                                             &emitOpCAPIHeader);

static mlir::GenRegistration
    genOpCAPIImpl("gen-op-capi-impl", "Generate operation C API implementation",
                  &emitOpCAPIImpl);