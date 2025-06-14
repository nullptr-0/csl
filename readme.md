# **Config Schema Language (CSL)**

**Config Schema Language (CSL)** is a language that describes the schema of a configuration. It combines readability with formal structure. The syntax takes inspiration from TypeScript, C++ and Rust, using familiar braces and type annotations while adding declarative constraints.

---

### **Config Schema Language (CSL) Example**
```CSL
// Root configuration structure
config MyAppConfig {
  // Basic key-value pairs (mandatory by default)
  app_name: string;
  version: string;
  environment: "dev" | "staging" | "prod" = "dev"; // Optional default value

  // Optional key (denoted by '?')
  timeout?: number @min(0) @max(60); // Annotations for validation

  // Nested table
  database: {
    host: string;
    port: number @range(1024, 65535);
    credentials?: { // Optional sub-table
      username: string;
      password: string;
    };
  };

  // Array of objects
  endpoints: {
    path: string;
    method: "GET" | "POST" | "PUT";
    rate_limit?: number;
  }[];

  // Unspecified table/array
  metadata?: any{};
  debug_flags?: any[];

  // Nested table with optional unspecified content
  services: {
    name: string;
    config?: any{}; // Any subtable allowed
  }[];

  // Array of unspecified tables
  raw_data?: any{}[];

  // Key relationships & constraints
  constraints {
    // Conflicts: 'ssl' cannot coexist with 'insecure_mode'
    conflicts database.ssl with insecure_mode;

    // Dependency: If 'credentials' exists, 'environment' must be "prod"
    requires database.credentials => environment == "prod";

    // Custom validation: If 'environment' is "prod", 'timeout' must be > 10
    validate environment == "prod" ? timeout > 10 : true;

    // Conflicts: "debug_flags" cannot coexist with "production_mode"
    conflicts debug_flags with production_mode;

    // Dependency: If "metadata" exists, "version" must exist (no value check)
    requires metadata => version;

    // Mixed dependency: If "services" exists, "app_name" must match a regex
    requires services => app_name @regex("^svc-.*");
  };
}
```

---

### **Key Features**

1. **Token Format**
   - **Comments**: `//` marks the rest of the line as a comment, except when inside a string.
   - **Numbers**: Decimal numbers (e.g., `123`, `3.14` and `1e5`), binary numbers (start with `0b`), octal numbers (start with `0o`), hexadecimal numbers (start with `0x`) and special numbers (`nan`, `+nan`, `-nan`, `inf`, `+inf` and `-inf`). Use a `_` to enhance readability (e.g., `1_000_000`).
   - **Booleans**: `true`/`false`.
   - **Strings**: Normal string literals (`"..."`, where escape sequences are allowed) and raw string literals (`R"delim(...)delim"`, where the delimiter matches `[a-zA-Z0-9!\"#%&'*+,\-.\/:;<=>?\[\]^_{|}~]{0,16}`). The valid escape sequences in normal strings are:
     | Escape Sequence                             | Notes                                                  |
     |---------------------------------------------|--------------------------------------------------------|
     | `\a`                                        | `\x07` alert (bell)                                    |
     | `\b`                                        | `\x08` backspace                                       |
     | `\t`                                        | `\x09` horizonal tab                                   |
     | `\n`                                        | `\x0A` newline (or line feed)                          |
     | `\v`                                        | `\x0B` vertical tab                                    |
     | `\f`                                        | `\x0C` form feed                                       |
     | `\r`                                        | `\x0D` carriage return                                 |
     | `\"`                                        | quotation mark                                         |
     | `\'`                                        | apostrophe                                             |
     | `\?`                                        | question mark (used to avoid trigraphs)                |
     | `\\`                                        | backslash                                              |
     | `\` + backtick                              | backtick                                               |
     | `\` + up to 3 octal digits                  |                                                        |
     | `\x` + any number of hex digits             |                                                        |
     | `\u` + 4 hex digits (Unicode BMP)           |                                                        |
     | `\U` + 8 hex digits (Unicode astral planes) |                                                        |
     | any other invalid sequences                 | content without `\` (e.g., `\c` is interpreted as `c`) |
   - **Date-times**: ISO 8601 date-times.
   - **Durations**: ISO 8601 durations or the following shorthand format:
     | Unit          | Suffix | Example       | Notes                              |
     |---------------|--------|---------------|------------------------------------|
     | Year          | `y`    | `1y`          |                                    |
     | Month         | `mo`   | `6mo`         | Avoids conflict with `m` (minute). |
     | Week          | `w`    | `2w`          |                                    |
     | Day           | `d`    | `3d`          |                                    |
     | Hour          | `h`    | `4h`          |                                    |
     | Minute        | `m`    | `30m`         |                                    |
     | Second        | `s`    | `15s`         |                                    |
     | Millisecond   | `ms`   | `200ms`       |                                    |
   - **Identifiers (Keys)**: `[a-zA-Z_][a-zA-Z0-9_]*` or content enclosed in backticks (`) using the same rules as string literals.
     ```CSL
     key: number;
     `quotedKey1`: number;
     R`delim(quotedKey2)delim`: number;
     ```

2. **Type System**
   - **Primitives**: `string`, `number`, `boolean`, `datetime`, `duration`.
   - **Enums**: `"GET" | "POST"` (union of literals).
   - **Tables**: `{ key: type; ... }` (nestable objects).
   - **Arrays**: `type[]` (e.g., `string[]`, `{...}[]`).
   - **Union Type**:
     Use the | operator to allow multiple types or literals.
     ```CSL
     port: number | string;  // Number or string
     debug: boolean | "verbose"; // Boolean or the literal "verbose"
     ```
     Specifying a type that a literal in the union has is not allowed.
     ```CSL
     config Example {
       // ❌ Invalid: "info" is a string literal
       log_level: string | "info";
     }
     ```

3. **Optionality**
   - Mandatory keys: `key: type;`
   - Optional keys: `key?: type;`

4. **Annotations** (Inline Validation)
   - There are two types of annotations: local annotations and global annotations.
   - Local annotations:
     - Local annotations only apply to a primitive type or an expression.
     - `@min(10)`, `@max(100)` – Numeric bounds. Applies only to number type.
     - `@range(10, 100)` – Numeric range. Applies only to number type.
     - `@int`, `@float` – Numeric type. Applies only to number type.
     - `@regex("^[a-z]+$")` – Regex for strings. Applies only to strings type.
     - `@start_with("/usr/")` – Starts-with assertion for strings. Applies only to strings type.
     - `@end_with(".csl")` – Ends-with assertion for strings. Applies only to strings type.
     - `@contain("temp")` – Contains assertion for strings. Applies only to strings type.
     - `@min_length(10)`, `@max_length(100)`, `@length(50)` – String length assertion. Applies only to strings type.
     - `@format(email)` – Built-in formats. Applies only to strings type. Available formats:
       - email: ```(?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\.){3}(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\])```
       - uuid: ```([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})```
       - ipv4: ```(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])```
       - ipv6: ```(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,7}:|(?:[0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,5}(?::[0-9a-fA-F]{1,4}){1,2}|(?:[0-9a-fA-F]{1,4}:){1,4}(?::[0-9a-fA-F]{1,4}){1,3}|(?:[0-9a-fA-F]{1,4}:){1,3}(?::[0-9a-fA-F]{1,4}){1,4}|(?:[0-9a-fA-F]{1,4}:){1,2}(?::[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:(?::[0-9a-fA-F]{1,4}){1,6}|:((?::[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(?::[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]+|::(ffff(:0{1,4}){0,1}:){0,1}(25[0-5]|(2[0-4][0-9]|(1[01][0-9]|[1-9]?[0-9]))\.){3}(25[0-5]|(2[0-4][0-9]|(1[01][0-9]|[1-9]?[0-9])))```
       - url: ```(?:(?:https?|ftp):\/\/)?(?:\S+(?::\S*)?@)?((?:(?!-)[A-Za-z0-9-]{0,62}[A-Za-z0-9]\.)+[A-Za-z]{2,6}|(?:\d{1,3}\.){3}\d{1,3})(?::\d{2,5})?(?:\/[^\s?#]*)?(?:\?[^\s#]*)?(?:#[^\s]*)?```
       - phone: ```\+?[0-9]{1,4}?[-. ]?\(?[0-9]{1,4}?\)?[-. ]?[0-9]{1,4}[-. ]?[0-9]{1,9}```
   - Global annotations:
     - Global annotations apply to the key.
     - `@deprecated("This key is deprecated.")` – Marks a key as deprecated. This annotation always evaluates to true.
   - Local annotations follow the specific type they apply to, while global annotations are placed at the end of the key declaration.
     ```CSL
     retries: "unlimited" | number @min(0) @deprecated("This key is deprecated.");
     ```

5. **Constraints Block**
   - **Conflicts**: `conflicts <key> with <key>;`
   - **Dependencies**: `requires <key> => <condition>;`
   - **Custom Logic**: `validate <expression>;` (boolean logic).
   - Constraints block can appear anywhere in the scope, but only one per scope.

6. **Expressions**
   - Reference keys: `environment == "prod"`, `database.port > 1024`.
   - Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`.
   - Logical operators (from highest priority to lowest priority): `!`, `&&`, `||`.
   - Ternary operator: `? :`.

7. **Unspecified Tables/Arrays**
   - To allow a key to be **any table** or **any array** (without specifying internal structure):
   - **Any Table**: Use `any{}` as the type. Accepts any table value (treated as a generic key-value table).
   - **Any Array**: Use `any[]` as the type. Accepts any array (elements can be mixed types).
   - Useful for "passthrough" configurations or avoiding over-constraining.
   - **Flexibility vs. Safety**: `any{}`/`any[]` sacrifices validation for flexibility. Use sparingly.
   - **Tooling Support**: Linters/IDEs should warn about unconstrained `any{}` usage.
   - `any{}`/`any[]` cannot have nested constraints/annotations. They are "opaque" types.

8. **Dependency Syntax**
   - `requires key_A => key_B`: Ensures **existence** of `key_B` (not value of `key_B` being positive) if `key_A` exists (not value of `key_A` being positive).
   - Combine with annotations for value-based constraints:
     ```CSL
     requires ssl => domain @regex("\\.example\\.com$");
     ```

9. **Explicit Existence Checks**
   - Use `exists(key)` in expressions for custom logic:
     ```CSL
     validate exists(database.credentials) ? environment == "prod" : true;
     ```

10. **Wildcard Key (`*`)**:
   - Indicates "any key name is allowed here."
   - The value type after `:` enforces the structure for **all matched entries** in the table.
   - A wildcard key matches any keys unless they are explicitly defined. Explicit keys take precedence over wildcards.
     ```CSL
     config BuildConfig {
       // All keys under "target" must have values of type `{ lib_path: string; bin_path: string }`
       target: {
         x86: { lib_path: string; }; // Explicit key
         *: { lib_path: string; bin_path: string; }; // Wildcard
       };
     }
     ```
     This means:
     - `target` is a table where **any key** (e.g., `riscv`, `arm`, etc.) is allowed.
     - `target.x86` **only** requires `lib_path` (wildcard `bin_path` is ignored for this key).
     - Any other key (e.g., `arm`) **must** be a table with `lib_path` and `bin_path` (both strings).
   - Wildcard keys works with nested structures. For example:
     ```CSL
     platforms: {
       *: {  // Dynamic platform names (e.g., "windows", "linux")
         arch: {
           *: { lib_path: string; };  // Dynamic arch keys (e.g., "x86", "arm64")
         };
       };
     };
     ```
   - You can enforce additional rules on the dynamic keys or their values using a `constraints` block:
     ```CSL
     config BuildConfig {
       target: {
         *: {
           lib_path: string @starts_with("/usr/");
           bin_path: string;
         };
       };

       constraints {
         // Require at least one key in "target"
         validate count_keys(target) > 0;

         // All keys in "target" must match a regex (e.g., lowercase/numbers)
         validate all_keys(target) match "^[a-z0-9_]+$";
       };
     }
     ```

11. **Batch Key Validation**
   - `count_keys(table)` – Returns the number of keys in a table.
   - `all_keys(table)` – Returns the keys of a table for validation (e.g., regex checks).
   - `wildcard_keys(table)` – Returns the wildcard keys of a table for validation (e.g., regex checks).
   - Annotations for string can be used with `all_keys` and `wildcard_keys` to validate the name of the keys.

12. **Subset Validation**
   Use `subset` function in the `constraints` block:
   ```CSL
   constraints {
     // Basic subset check (all elements of `selected` exist in `allowed`)
     validate subset(selected_features, allowed_features);

     // Optional: Specify comparison keys for objects (e.g., match by id)
     validate subset(selected_plugins, available_plugins, [id]);
   }
   ```

---

### **Semantic Rules**
- **Scoping**: Constraints apply to the current block (e.g., nested tables have their own `constraints`).
- **Path References**: Use dot notation (`database.credentials.username`) to reference nested keys.
- **Default Values**: Assign with `= value` (e.g., `environment = "dev"`).

---

### **Scoping Rules**
1. **Constraints in a nested block can only reference keys within their immediate scope**.
   - No access to parent/ancestor keys. In a scope, only local keys or **relative nested keys** are allowed.
   - All keys in constraints must exist **within the same block** where the `constraints` are defined.
   - **Tooling**: A parser/validator would flag any reference to keys outside the current block’s scope.
   - **Valid: Local Constraints Only**
     ```CSL
     config Server {
       database: {
         ssl: boolean;
         port: number;
         constraints {
           // ✅ Valid: `ssl` and `port` are in the same block
           conflicts ssl with port;
         };
       };
       insecure_mode: boolean;

       constraints {
         // ✅ Valid: Constraints can reference keys in the same block (`insecure_mode`)
         // and nested keys via their **relative path** (`database.ssl`).
         conflicts database.ssl with insecure_mode;
       };
     }
     ```
   - **Invalid: Nested Constraints Referencing Parent Keys**
     ```CSL
     config Example {
       log_level: "debug" | "info";
       logger: {
         format: string;
         constraints {
           // ❌ Invalid: `log_level` is in the parent scope
           conflicts format with log_level;
         };
       };
     }
     ```
   - **Local-Only Rules For Wildcard Keys**: Constraints inside the `*` block apply to **all instances** of dynamic keys. For example:
     ```CSL
     target: {
       *: {
         lib_path: string;
         constraints {
           // Applies to every key under "target":
           // If `lib_path` contains "temp", `bin_path` must exist.
           validate lib_path @contains("temp") ? exists(bin_path) : true;
         };
       };
     };
     ```

2. **Parent constraints cannot be overridden by child constraints**.
   - Constraints are **additive**: Parent and child constraints *both* apply.
   - Conflicting constraints (e.g., a parent says `A` is required, a child says `A` is forbidden) will make both constraints invalid.
   - Constraints are **never overridden** — they are cumulative. For example:
     ```CSL
     config Example {
       constraints {
         requires nested.a => b;  // Parent rule
       };
       nested: {
         a?: boolean;
         constraints {
           requires a => c;  // Child rule (additive)
         };
       };
     }
     ```
     If `nested.a` exists, **both** `b` (from root) and `c` (from nested) must exist.
     If `nested.a` and `nested.c` exist but `b` doesn’t, validation **fails** due to the root constraint.

---

### **`subset` Key Rules**
1. **Primitive Arrays** (strings, numbers, etc.):
   - Omit the `properties` parameter. Uses strict equality (`"auto" === "auto"`).
   - Example: `subset(selected_ports, allowed_ports)`.

2. **Object Arrays**:
   - Use `properties` to specify which keys to compare (e.g., `[id]`).
   - If omitted, validates by **full object equality** (rarely useful, so tools may warn).

3. **Composite Keys**:
   - Provide multiple properties (e.g., `[region, env]`). All must match.

4. **Edge Cases**:
   - Empty `source_array` is always valid.
   - Empty `target_array` + non-empty `source_array` → invalid.

5. **Tooling Behavior**:
   - **Validation**:
     - Ensure `properties` exist in both `source_array` and `target_array` schemas.
     - Error if `properties` are specified for primitive arrays.
   - **Autocomplete**: Suggest available properties for object arrays.

---

### **Tooling Integration**
- **Validation**: Use a CSL parser to check config files against the schema.
- **Autocomplete**: IDEs can suggest keys/types based on the schema.
- **Documentation**: Generate human-readable docs from CSL files.

---

### **Why CSL Works**

**CSL** strikes a deliberate balance between flexibility and rigor, making it effective for real-world configuration management:

1. **Human-Centered Design**  
   - **Familiar syntax** (TypeScript/C++/Rust-inspired) reduces cognitive load. Developers intuitively understand braces, type annotations, and `key: value` structures.  
   - **Annotations** keep validation rules inline with type definitions, minimizing context switching.  
   - **Wildcard keys** (`*`) and `any{}`/`any[]` enable gradual schema adoption—teams can start loosely and tighten constraints incrementally.

2. **Precision Without Verbosity**  
   - **Union types** (`"dev" | "prod"`) and **enums** enforce strict allowed values without bloated boilerplate.  
   - **Expressive constraints** (e.g., `conflicts`, `requires`) declaratively model complex key relationships, replacing fragile ad-hoc validation scripts.  

3. **Context-Aware Validation**  
   - **Scoped constraints** ensure rules apply only to their logical context, preventing unintended side effects.  
   - **Absolute/relative paths** (`database.port`) enable cross-key validation while nested constraints keep local logic self-contained.  

4. **Dynamic Config Support**  
   - **Wildcard-driven schemas** validate dynamic keys (e.g., environment-specific settings) without sacrificing structure.  
   - **Batch key checks** (`all_keys()`, regex validations) enforce naming conventions or key patterns at scale.  

5. **Tooling-Ready Structure**  
   - **Explicit types and constraints** enable IDE autocomplete, linting, and documentation generation out-of-the-box.  
   - **`subset` validation** and **existence checks** (`exists()`) cover advanced use cases like feature toggles or plugin dependencies.  

6. **Safety by Default**  
   - **Mandatory keys** enforce critical properties upfront.  
   - **Conflict resolution** (`conflicts X with Y`) prevents invalid states that manual reviews often miss.  

**CSL** avoids the extremes of rigid, unmaintainable schemas or overly permissive "anything goes" configurations. It codifies best practices—type safety, proactive validation, and clear relationships—while staying adaptable to evolving requirements.