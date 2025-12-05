CSL IntelliJ Plugin

Overview
Provides CSL language integration in IntelliJ via LSP and IntelliJ's API.

Features
- Language support for `CSL` using local CSL server.
- Diagnostics: real-time error checking.
- Completion: suggest code completions as you type.
- Hover: show type information and documentation on hover.
- References: find all references to a symbol.
- Rename: rename symbols across the schema.
- Line comments `//` toggle.
- Brace matching for `{`, `}`, `(`, `)`, `[`, and `]`.
- Code folding for schemas, tables, and comments.
- Editor and project view `Generate CSL HTML Docs` context action: choose an output directory and opens `index.html` when available.

Prerequisites
- JDK 25+ and Gradle.
- IntelliJ IDEA 2025.2+.

Getting the CSL executable
1) Build native core executable:
   - Ensure CMake and a C++ compiler are installed.
   - From `impl/core`, run a standard CMake build producing the `csl` binary.
2) Copy the resulting executable to:
   - Windows: `impl/extension/IntelliJ/src/main/resources/bin/windows/csl.exe`
   - Linux: `impl/extension/IntelliJ/src/main/resources/bin/linux/csl`
   - macOS: `impl/extension/IntelliJ/src/main/resources/bin/macos/csl`

How the plugin finds the server
- On startup, the plugin extracts the platform-specific `csl` binary from `src/main/resources/bin/<os>/` into the plugin install directory and marks it executable.
- It then launches the server as `csl --langsvr --stdio`.

Build and Run
1) In this directory:
   - `./gradlew build`
   - `./gradlew runIde`
2) In the sandboxed IDE:
   - Open or create a project containing `.csl` files.
   - Open a `.csl` file to start the language server.
   - Right‑click the file in the editor or Project view → `Generate CSL HTML Docs`.

Distributions
- `./gradlew buildPlugin` produces a `.zip` under `build/distributions` for manual install.

Notes
- If the `csl` executable is missing, the plugin will show an error when LSP or HTML doc generation is invoked.
- Diagnostics, completion, hover, references, rename, folding, and more are provided by the CSL language server.
