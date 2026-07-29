# B Language Support for VS Code

This extension adds syntax highlighting and language support for the B programming language in Visual Studio Code.

## Features

- **Syntax Highlighting**: Recognizes and colorizes B language constructs
  - Keywords (types, ownership, control flow, declarations)
  - Strings and escape sequences
  - Numbers (integers and floats)
  - Comments (line and block)
  - Operators and punctuation, including `::` and `?`

- **Smart Indentation**: Automatic indentation for braces and control structures

- **Bracket Matching**: Auto-completion and matching of brackets, braces, and quotes

## Installation

### Manual Installation

1. Copy the `vscode-ext` folder to your VS Code extensions directory:
   - **Linux/Mac**: `~/.vscode/extensions/`
   - **Windows**: `%USERPROFILE%\.vscode\extensions\`

2. Rename the folder to `b-language-<version>`

3. Restart VS Code

### Development Installation

```bash
cd vscode-ext
code --install-extension .
```

## Supported File Types

- `.b` - B source code files

## Syntax Highlighting Categories

| Category | Examples |
|----------|----------|
| Keywords (Type) | `int`, `float`, `bool`, `void`, `own`, `mut` |
| Keywords (Control) | `if`, `else`, `for`, `while`, `return`, `switch`, `some` |
| Keywords (Declaration) | `struct`, `enum`, `typedef`, `const`, `namespace`, `drop` |
| Keywords (Module) | `import`, `using`, `pub` |
| Operators | `sizeof`, `new`, `len` |
| Constants | `true`, `false`, `none` |
| Strings | `"hello"`, `'w'` |
| Numbers | `42`, `3.14` |
| Comments | `// comment`, `/* block */` |

## Configuration

The extension uses VS Code's built-in theming system. Colors are determined by your selected VS Code theme.

To customize colors, add rules to your VS Code `settings.json`:

```json
"editor.tokenColorCustomizations": {
  "textMateRules": [
    {
      "scope": "keyword.type.b",
      "settings": {
        "foreground": "#0000FF"
      }
    }
  ]
}
```

## Contributing

Report issues or suggest improvements on the [B GitHub repository](https://github.com/ital87/B).

## License

MIT License - See the main B repository for details.
