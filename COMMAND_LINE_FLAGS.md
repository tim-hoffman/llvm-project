# OpCAPIGen Command-Line Flags

The `mlir-tblgen` operation C API generator now supports fine-grained control over what gets generated through command-line flags.

## Available Flags

### `--dialect-name=<name>` (Required)
Specifies the dialect name to use for generated functions.
- **Form**: `mlirCreate<Dialect><OpName>`
- **Example**: `--dialect-name=MyDialect` generates `mlirCreateMyDialectMyOp(...)`

### `--gen-create` (Default: true)
Controls generation of operation create functions.
- **Enables**: `mlirCreate{Dialect}{Op}(...)` functions
- **Disable with**: `--gen-create=false`

### `--gen-operand-getters` (Default: true)
Controls generation of operand getters.
- **Enables**:
  - Non-variadic: `mlir{Dialect}{Op}Get{Operand}(...)`
  - Variadic: `mlir{Dialect}{Op}Get{Operand}Count(...)` and `mlir{Dialect}{Op}Get{Operand}(op, index)`
- **Disable with**: `--gen-operand-getters=false`

### `--gen-operand-setters` (Default: true)
Controls generation of operand setters.
- **Enables**:
  - Non-variadic: `mlir{Dialect}{Op}Set{Operand}(...)`
  - Variadic: `mlir{Dialect}{Op}Set{Operand}(op, count, values)`
- **Disable with**: `--gen-operand-setters=false`

### `--gen-attribute-getters` (Default: true)
Controls generation of attribute getters.
- **Enables**: `mlir{Dialect}{Op}Get{Attribute}(...)`
- **Disable with**: `--gen-attribute-getters=false`

### `--gen-attribute-setters` (Default: true)
Controls generation of attribute setters.
- **Enables**: `mlir{Dialect}{Op}Set{Attribute}(...)`
- **Disable with**: `--gen-attribute-setters=false`

### `--gen-result-getters` (Default: true)
Controls generation of result getters.
- **Enables**:
  - Non-variadic: `mlir{Dialect}{Op}Get{Result}(...)`
  - Variadic: `mlir{Dialect}{Op}Get{Result}Count(...)` and `mlir{Dialect}{Op}Get{Result}(op, index)`
- **Disable with**: `--gen-result-getters=false`

### `--gen-region-getters` (Default: true)
Controls generation of region getters.
- **Enables**:
  - Non-variadic: `mlir{Dialect}{Op}Get{Region}(...)`
  - Variadic: `mlir{Dialect}{Op}Get{Region}Count(...)` and `mlir{Dialect}{Op}Get{Region}(op, index)`
- **Disable with**: `--gen-region-getters=false`

## Usage Examples

### Generate Everything (Default Behavior)
```bash
mlir-tblgen -gen-op-capi-header --dialect-name=MyDialect MyOps.td -o MyOps.h
mlir-tblgen -gen-op-capi-impl --dialect-name=MyDialect MyOps.td -o MyOps.c
```

### Generate Only Create Functions
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-operand-getters=false \
  --gen-operand-setters=false \
  --gen-attribute-getters=false \
  --gen-attribute-setters=false \
  --gen-result-getters=false \
  --gen-region-getters=false \
  MyOps.td -o MyOps.h
```

### Generate Only Accessors (No Create Functions)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-create=false \
  MyOps.td -o MyOps.h
```

### Generate Read-Only API (Getters Only, No Setters)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-operand-setters=false \
  --gen-attribute-setters=false \
  MyOps.td -o MyOps.h
```

### Generate Only Operand and Result Getters
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-create=false \
  --gen-operand-setters=false \
  --gen-attribute-getters=false \
  --gen-attribute-setters=false \
  --gen-region-getters=false \
  MyOps.td -o MyOps.h
```

### Generate Only Setters (Custom Getters)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-create=false \
  --gen-operand-getters=false \
  --gen-attribute-getters=false \
  --gen-result-getters=false \
  --gen-region-getters=false \
  MyOps.td -o MyOps.h
```

### Minimal Generation (Only Attribute Getters)
```bash
mlir-tblgen -gen-op-capi-header \
  --dialect-name=MyDialect \
  --gen-create=false \
  --gen-operand-getters=false \
  --gen-operand-setters=false \
  --gen-attribute-setters=false \
  --gen-result-getters=false \
  --gen-region-getters=false \
  MyOps.td -o MyOps.h
```

## Use Cases

### Scenario 1: Custom Creation Logic
If you want to provide your own custom operation creation functions but still want generated accessors:
```bash
--gen-create=false
```

### Scenario 2: Read-Only API
If you only want getters (no setters) to enforce immutability:
```bash
--gen-operand-setters=false \
--gen-attribute-setters=false
```
This is useful for creating a query-only API where operations shouldn't be modified after creation.

### Scenario 3: Write-Only Initialization API
If you're creating an initialization API and want setters but will use custom logic for getters:
```bash
--gen-operand-getters=false \
--gen-attribute-getters=false \
--gen-result-getters=false \
--gen-region-getters=false
```

### Scenario 4: Minimal Footprint
If you only need a subset of functionality to reduce binary size or compilation time:
```bash
# Only what you need
--gen-create=true \
--gen-operand-getters=true \
--gen-operand-setters=false \
--gen-attribute-getters=false \
--gen-attribute-setters=false \
--gen-result-getters=true \
--gen-region-getters=false
```

### Scenario 5: Incremental Migration
When gradually migrating existing C API to generated code:
1. Start with all flags set to `false` and keep your existing code
2. Enable `--gen-create=true` to generate create functions
3. Test and validate
4. Gradually enable other categories (e.g., `--gen-operand-getters=true`)
5. Remove your hand-written equivalents as you enable each flag

### Scenario 6: Hybrid Custom/Generated
Mix custom implementations with generated code:
```bash
# Generate getters, but use custom setters with validation logic
--gen-operand-setters=false \
--gen-attribute-setters=false
```

## Flag Combinations

### All Getters Only
```bash
--gen-operand-setters=false \
--gen-attribute-setters=false
```

### All Setters Only
```bash
--gen-operand-getters=false \
--gen-attribute-getters=false \
--gen-result-getters=false \
--gen-region-getters=false
```

### Operands and Attributes Only
```bash
--gen-create=false \
--gen-result-getters=false \
--gen-region-getters=false
```

### Results and Regions Only
```bash
--gen-create=false \
--gen-operand-getters=false \
--gen-operand-setters=false \
--gen-attribute-getters=false \
--gen-attribute-setters=false
```

## Notes

- **All flags default to `true`**: By default, everything is generated
- **Flags apply to both header and implementation**: Use the same flags for `-gen-op-capi-header` and `-gen-op-capi-impl`
- **Dialect name is required**: You must always specify `--dialect-name`
- **Flag syntax**: Use `--flag-name=true` or `--flag-name=false` (or omit `=true` since it's the default)
- **Independent control**: Getters and setters are now independently controllable for maximum flexibility

## Benefits

1. **Reduced Code Size**: Only generate what you need
2. **Faster Compilation**: Less generated code means faster compile times
3. **Custom Implementations**: Disable categories where you want custom behavior
4. **Incremental Adoption**: Gradually adopt generated code
5. **Flexibility**: Mix generated and hand-written code as needed
6. **Read-Only APIs**: Generate getter-only APIs for immutability guarantees
7. **Validation**: Implement custom setters with validation while using generated getters
8. **Separation of Concerns**: Independently control read vs write operations
