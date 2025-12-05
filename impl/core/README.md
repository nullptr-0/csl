# csl: A Config Schema Language Utility

A C++20 implementation of a CSL parser and language server.

## Features

- **CSL Parsing**: Validates and parses CSL files.
- **Language Server Support**: LSP integration via standard IO, socket, and named pipe.
- **Cross-Platform**: Supports Windows, Unix-like systems and NodeJS environment.

## Dependencies

- **C++20 Compiler** (e.g., GCC 11+, Clang 12+, MSVC 2022+)
- **CMake 3.12+**

### WebAssembly-specific Dependencies
- **Python 3.8+**
- **Emscripten SDK** (will be automatically downloaded and setup by the build script)
- **Ninja Build System** (Windows-specific, will be automatically downloaded and setup by the build script)

## Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/nullptr-0/csl.git
   cd csl
   ```
2. Build from source:
   - Build the native version:
   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```
   - Build WebAssembly module:
   ```bash
   ./BuildWasm
   ```

## Usage

### Test a CSL File
```bash
path/to/csl --test path/to/input.csl
```
Outputs error/warning listings (and debug information if DEBUG preprocessor definition is present when building).
Throws exceptions if DEBUG preprocessor definition is present when building.

### Run Language Server
1. **Standard IO Mode**:
   ```bash
   path/to/csl --langsvr --stdio
   ```
2. **Socket Mode**:
   ```bash
   path/to/csl --langsvr --socket=8080
   ```
   or
   ```bash
   path/to/csl --langsvr --port=8080
   ```
3. **Named Pipe Mode**:
   ```bash
   path/to/csl --langsvr --pipe=PipeName
   ```

## Project Structure
```
├── CMakeLists.txt
├── driver/          # Main driver
├── lexer/           # Lexer
├── parser/          # Parser
├── langsvr/         # Language Server
└── shared/          # Common utilities
```

## License
Apache 2.0 License

## Contributing
1. Fork the repository
2. Create a feature branch
3. Submit a pull request with tests
4. Follow the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)

## Feedback
1. Search in the issues in case a similar problem has already been reported
2. Gather as much and detailed information as you can
3. Create an issue
4. Follow the [“How To Ask Questions The Smart Way” Guidelines](http://www.catb.org/~esr/faqs/smart-questions.html)
