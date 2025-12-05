# CSL Language Service for Visual Studio Code

![Version](https://img.shields.io/badge/version-0.0.1-blue)
[![VS Code Version](https://img.shields.io/badge/vscode-%3E%3D1.106.1-blueviolet)](https://code.visualstudio.com/)
[![Built with TypeScript](https://img.shields.io/badge/Built%20With-TypeScript-3178c6)](https://www.typescriptlang.org/)
[![Powered by LSP](https://img.shields.io/badge/Powered%20By-Language%20Server%20Protocol-3c8dbc)](https://microsoft.github.io/language-server-protocol/)
[![License](https://img.shields.io/badge/License-Apache%202.0-green)](https://www.apache.org/licenses/LICENSE-2.0.html)

A VS Code extension providing CSL language support. Built on top of a native CSL implementation.

![Extension Demo](https://raw.githubusercontent.com/nullptr-0/csl/refs/heads/main/impl/extension/VSCode/img/demo.png)

## Features

### ✨ Core Capabilities

#### 🖍️ Semantic Highlighting

* Colorizes CSL-specific syntax elements:

  * **Date-times** (`2023-01-01T12:00:00Z`)
  * **Booleans** (`true` / `false`)
  * **Numbers** (`0xff`, `1.5e2`)
  * **Identifiers** (`keyName`)
  * **Strings** (`"a string"`)
  * **Punctuators**
  * **Operators**
  * **Comments** (`// a comment`)

#### 🛠️ Document Diagnostics

* Reports **errors and warnings** with exact **line/column positioning**

#### 🔍 Hover Information

* Provides **hover cards** for tables and arrays with:

  * Type info
  * Optionality
  * Defined location (line & column)
  * Default value

#### 🔧 Formatting

* Formats CSL files based on parsed structure

#### 📚 Go to Definition

* Navigates to the definition of a key

#### 🔁 Renaming

* Renames **all references** of a key
* Uses token-to-structure mapping to rename safely and accurately

#### 🔗 Find References

* Finds **all references** of a key in a document

#### 🔄 Ranges Folding

* Supports folding for:

  * **Schemas and Tables**
  * **Multi-line comments**

#### 🔡 Completion Suggestions

* Auto-suggests known keys in scope
* Provides **context-aware completions** including:

  * Dot-navigation (`table.key`)
  * Details and definitions for each completion

#### 📄 HTML Docs generation
* Generates **HTML documentation** for CSL files
* Supports CSL files in editor and explorer view
* Opens `index.html` when generation is completed

#### 🧩 Language Server Integration
* Powered by the [native CSL implementation](https://github.com/nullptr-0/csl)


### 🧰 Editor Integration
* `.csl` file association
* Bracket matching for tables/arrays
* Comment toggling (`//` syntax)
* Auto-closing for brackets and quotations

## Installation

### Manual Installation
```bash
git clone https://github.com/nullptr-0/csl.git
cd csl/impl/extension/VSCode
npm install
# Build and copy native CSL core to "core" directory
vsce package
code --install-extension csl-0.0.1.vsix
```

## Configuration

### Semantic Token Mapping
Customize colors via `settings.json`:
```json
{
  "editor.semanticTokenColorCustomizations": {
    "[Your Theme]": {
      "rules": {
        "datetime": {"foreground": "#2ecc71"},
        "boolean": {"foreground": "#e74c3c", "fontStyle": "bold"}
      }
    }
  }
}
```
Valid semantic tokens are: `datetime`, `number`, `boolean`, `keyword`, `type`, `identifier`, `punctuator`, `operator`, `comment`, `string`, and `unknown`

## Development

### Prerequisites
- Node.js 24.10.1+ (older versions may also work but not recommended)
- VS Code 1.106.1+ (older versions may also work but not recommended)
- Native CSL core copied to `core` directory ([see core implementation](https://github.com/nullptr-0/csl))

### Build Process
```bash
# Install dependencies
npm install

# Development mode (watch files)
npm run watch

# Production build
npm run package

# Linting
npm run lint
```

### Architecture
```
├── core/
│   └── ...                        # CSL binary and/or module
├── img/
│   ├── demo.png                   # Extension Demo
│   └── csl.png                    # CSL icon
├── src/
│   └── extension.ts               # Extension entry point
├── .gitignore
├── .vscodeignore
├── esbuild.js
├── eslint.config.mjs
├── language-configuration.json
├── LICENSE.txt
├── package.json
├── README.md
└── tsconfig.json
```

## License
- **Core Implementation**: [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0.html)
- **Extension**: [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0.html)

## Report Issues
[GitHub Issues](https://github.com/nullptr-0/csl/issues)