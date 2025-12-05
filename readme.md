# Config Schema Language (CSL)

CSL is a concise, type-safe language for describing configuration schemas. It blends familiar syntax (inspired by TypeScript/C++/Rust) with declarative validation, so teams can write readable schemas that IDEs and tooling can understand.

## Highlights
- Human-friendly types: `string`, `number`, arrays (`type[]`), tables (`{ ... }`), unions (`"dev" | "prod"`).
- Inline validation via annotations: `@min(1)`, `@regex("^[a-z]+$")`, `@format(email)`, etc.
- Constraint blocks for relationships: `conflicts a with b;`, `requires a => b;`, `validate expr;`.
- Wildcard keys and opaque types: `*:` for dynamic entries, `any{}` / `any[]` for passthrough areas.
- Language Server support (LSP) and Node/WASM builds for cross-platform usage.

## Quick Taste
```csl
config AppConfig {
  app_name: string;
  environment: "dev" | "prod" = "dev";
  database: {
    host: string @format(url);
    port: number @range(1024, 65535);
    ssl?: boolean;
  };

  constraints {
    conflicts database.ssl with insecure_mode;
    requires database.ssl => environment == "prod";
  };
}
```

## Repository Layout
- `impl/core/` — C++20 implementation: lexer, parser, language server, documentation generator.
- `impl/node/` — NodeJS wrapper loading the WASM build for CLI/LSP.
- `impl/extension/VSCode/` — VSCode extension wiring to the language server.
- `impl/extension/IntelliJ/` — IntelliJ plugin wiring to the language server.
- `docs/CSL.md` — Complete language overview and reference.
- `htdocs/` — Official site.

## Build and Use

### Prerequisites
- C++20 compiler (MSVC 2022+, Clang 12+, GCC 11+)
- CMake 3.12+
#### WebAssembly-specific
- **Python 3.8+**
- **Emscripten SDK** (will be automatically downloaded and setup by the build script)
- **Ninja Build System** (Windows-specific, will be automatically downloaded and setup by the build script)
#### Node Wrapper-specific
- Node.js 24.10.1+ (older versions may also work but not recommended)
#### VSCode Extension-specific
- Node.js 24.10.1+ (older versions may also work but not recommended)
- VS Code 1.106.1+ (older versions may also work but not recommended)
#### IntelliJ Plugin-specific
- JDK 25+ (older versions may also work but not recommended)
- Gradle
- IntelliJ IDEA 2025.2+ (older versions may also work but not recommended)

### Native C++ binary
From the repository root:
```bash
cmake -B build -S impl/core -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
This produces the `csl` and `csl-test` executable.

- Test a CSL file:
```bash
path/to/csl --test path/to/schema.csl
```
- Run the language server (choose one transport):
```bash
path/to/csl --langsvr --stdio
path/to/csl --langsvr --socket=8080
path/to/csl --langsvr --pipe=PipeName
```
- Run the full regression suite using the test runner:
```bash
# Linux / macOS (from build directory)
./csl-test --test ../tests

# Windows (from build directory)
.\Release\csl-test.exe --test ..\tests
```

### WebAssembly + Node wrapper
Build the WASM artifacts and the Node wrapper:
```bash
# Build the WASM module
impl/core/BuildWasm Release

# Then compile the Node wrapper
cd impl/node
npm install
npm run compile
```
- Use the Node CLI wrapper (LSP/parse commands are forwarded to the WASM module):
```bash
node ./out/csl.js --test path/to/schema.csl
node ./out/csl.js --langsvr --stdio
```

### VSCode extension
- Build the native `csl` binary (above) and place it under `impl/extension/VSCode/core/`.
- Install dependencies and compile the extension:
```bash
cd impl/extension/VSCode
npm install
vsce package
```
- This generates vsix package, which you can install in VSCode.

## Documentation
- Full language specification: `docs/CSL.md`

## Contributing
- Fork and create a feature branch.
- Keep changes aligned with the existing style.
- Add tests or examples where relevant and open a PR.

## License
Apache License 2.0. See `LICENSE.txt`.
