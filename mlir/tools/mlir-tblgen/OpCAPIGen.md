# OpCAPIGen Command-Line Flags

The `mlir-tblgen` operation C API generator now supports fine-grained control over what gets generated through command-line flags.

## Available Flags

### `--function-prefix=<prefix>` (Default: "mlir")
Specifies the prefix to use for all generated C API function names.
- **Form**: `{prefix}{Dialect}{Op}{Accessor}`
- **Example**: `--function-prefix=custom` generates `customCreateMyDialectMyOp(...)` instead of `mlirCreateMyDialectMyOp(...)`
- **Use Case**: Useful for avoiding naming conflicts or creating custom namespaced APIs

### `--dialect-name=<name>` (Required)
Specifies the dialect name to use for generated functions.
- **Form**: `{prefix}Create{Dialect}{OpName}`
- **Example**: `--dialect-name=MyDialect` generates `mlirCreateMyDialectMyOp(...)`

### `--gen-create` (Default: true)
Controls generation of operation create functions.
- **Enables**: `{prefix}Create{Dialect}{Op}(...)` functions
- **Disable with**: `--gen-create=false`

### `--gen-operand-getters` (Default: true)
Controls generation of operand getters.
- **Enables**:
  - Non-variadic: `{prefix}{Dialect}{Op}Get{Operand}(...)`
  - Variadic: `{prefix}{Dialect}{Op}Get{Operand}Count(...)` and `{prefix}{Dialect}{Op}Get{Operand}(op, index)`
- **Disable with**: `--gen-operand-getters=false`

### `--gen-operand-setters` (Default: true)
Controls generation of operand setters.
- **Enables**:
  - Non-variadic: `{prefix}{Dialect}{Op}Set{Operand}(...)`
  - Variadic: `{prefix}{Dialect}{Op}Set{Operand}(op, count, values)`
- **Disable with**: `--gen-operand-setters=false`

### `--gen-attribute-getters` (Default: true)
Controls generation of attribute getters.
- **Enables**: `{prefix}{Dialect}{Op}Get{Attribute}(...)`
- **Disable with**: `--gen-attribute-getters=false`

### `--gen-attribute-setters` (Default: true)
Controls generation of attribute setters.
- **Enables**: `{prefix}{Dialect}{Op}Set{Attribute}(...)`
- **Disable with**: `--gen-attribute-setters=false`

### `--gen-result-getters` (Default: true)
Controls generation of result getters.
- **Enables**:
  - Non-variadic: `{prefix}{Dialect}{Op}Get{Result}(...)`
  - Variadic: `{prefix}{Dialect}{Op}Get{Result}Count(...)` and `{prefix}{Dialect}{Op}Get{Result}(op, index)`
- **Disable with**: `--gen-result-getters=false`

### `--gen-region-getters` (Default: true)
Controls generation of region getters.
- **Enables**:
  - Non-variadic: `{prefix}{Dialect}{Op}Get{Region}(...)`
  - Variadic: `{prefix}{Dialect}{Op}Get{Region}Count(...)` and `{prefix}{Dialect}{Op}Get{Region}(op, index)`
- **Disable with**: `--gen-region-getters=false`

### `--gen-extra-class-methods` (Default: true)
Controls generation of C API wrappers for methods defined in `extraClassDeclaration`.
- **Enables**: C API wrappers for custom operation methods
- **Requirements**:
  - Only methods without parameters are supported
  - Return types must be convertible to C API types (bool, int, MlirType, MlirValue, etc.)
  - Methods with ArrayRef, SmallVector, or other container return types are skipped
- **Features**:
  - Automatically extracts and propagates documentation comments (`///` or `/* */` style)
  - Converts method names to CamelCase (e.g., `nameIsCompute` → `NameIsCompute`)
  - Handles dialect-specific return types (e.g., `::llzk::component::StructType` → `MlirType`)
  - Properly wraps/unwraps MLIR objects between C++ and C API
- **Example**: A method `bool nameIsCompute()` in `extraClassDeclaration` generates `bool {prefix}{Dialect}{Op}NameIsCompute(MlirOperation op)`
- **Disable with**: `--gen-extra-class-methods=false`

## Usage Examples

### Basic Usage (Default Behavior)
```bash
mlir-tblgen -gen-op-capi-header --dialect-name=MyDialect MyOps.td -o MyOps.h
mlir-tblgen -gen-op-capi-impl --dialect-name=MyDialect MyOps.td -o MyOps.c
```

### Custom Function Prefix
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --function-prefix=myproject \
  MyOps.td -o MyOps.h
# Generates: myprojectCreateMyDialectMyOp(...) instead of mlirCreateMyDialectMyOp(...)
```

### Read-Only API (Getters Only)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-operand-setters=false \
  --gen-attribute-setters=false \
  MyOps.td -o MyOps.h
```

### Minimal Footprint (Specific Functionality Only)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-create=true \
  --gen-operand-getters=true \
  --gen-operand-setters=false \
  --gen-attribute-getters=false \
  --gen-attribute-setters=false \
  --gen-result-getters=true \
  --gen-region-getters=false \
  --gen-extra-class-methods=false \
  MyOps.td -o MyOps.h
```

## Common Use Cases

### Custom Creation Logic
Provide your own operation creation functions but use generated accessors:
```bash
--gen-create=false
```

### Immutable Operations (Read-Only API)
Enforce immutability by generating only getters:
```bash
--gen-operand-setters=false --gen-attribute-setters=false
```

### Custom Setters with Validation
Generate getters but implement custom setters with validation logic:
```bash
--gen-operand-setters=false --gen-attribute-setters=false
```

### Incremental Migration Strategy
Gradually migrate from hand-written to generated code:
1. Start with all flags set to `false` and keep your existing code
2. Enable `--gen-create=true` to generate create functions
3. Test and validate
4. Gradually enable other categories (e.g., `--gen-operand-getters=true`)
5. Remove your hand-written equivalents as you enable each flag

### Custom Namespace/Branding
Avoid naming conflicts or create project-specific APIs:
```bash
--function-prefix=myproject
```
Useful when:
- Embedding MLIR in a larger project with existing "mlir*" prefixed functions
- Creating multiple independent C API bindings for different MLIR dialects
- Building a C API wrapper library with your own branding

### Expose Operation Helper Methods
Generate C API wrappers for custom methods in `extraClassDeclaration`:
- Enable with `--gen-extra-class-methods=true` (default)
- Disable with `--gen-extra-class-methods=false` to provide custom implementations

Useful for:
- Operation query methods (e.g., `isConstant()`, `nameIsCompute()`)
- Exposing operation-specific utility functions to C code

## Quick Reference: Flag Combinations

```bash
# All getters only
--gen-operand-setters=false --gen-attribute-setters=false

# All setters only
--gen-operand-getters=false --gen-attribute-getters=false \
--gen-result-getters=false --gen-region-getters=false

# Operands and attributes only
--gen-create=false --gen-result-getters=false --gen-region-getters=false \
--gen-extra-class-methods=false

# Results and regions only
--gen-create=false --gen-operand-getters=false --gen-operand-setters=false \
--gen-attribute-getters=false --gen-attribute-setters=false \
--gen-extra-class-methods=false

# Extra class methods only
--gen-create=false --gen-operand-getters=false --gen-operand-setters=false \
--gen-attribute-getters=false --gen-attribute-setters=false \
--gen-result-getters=false --gen-region-getters=false
```

## Notes

- **All flags default to `true`**: By default, everything is generated
- **Function prefix defaults to "mlir"**: Use `--function-prefix` to customize
- **Flags apply to both header and implementation**: Use the same flags for `-gen-op-capi-header` and `-gen-op-capi-impl`
- **Dialect name is required**: You must always specify `--dialect-name`
- **Flag syntax**: Use `--flag-name=true` or `--flag-name=false` (or omit `=true` since it's the default)
- **Independent control**: Getters and setters are now independently controllable for maximum flexibility
- **Extra class methods**: Only parameterless methods with C-compatible return types are generated
- **Documentation propagation**: Comments from TableGen extraClassDeclaration are automatically copied to generated C API

## Benefits

1. **Reduced Code Size**: Only generate what you need
2. **Faster Compilation**: Less generated code means faster compile times
3. **Custom Implementations**: Disable categories where you want custom behavior
4. **Incremental Adoption**: Gradually adopt generated code
5. **Flexibility**: Mix generated and hand-written code as needed
6. **Read-Only APIs**: Generate getter-only APIs for immutability guarantees
7. **Validation**: Implement custom setters with validation while using generated getters
8. **Separation of Concerns**: Independently control read vs write operations
9. **Custom Namespacing**: Use `--function-prefix` to avoid naming conflicts
10. **Automatic Method Wrapping**: Generate C bindings for custom operation methods defined in `extraClassDeclaration`
11. **Documentation Preservation**: TableGen comments are automatically propagated to generated C API functions
