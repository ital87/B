# Arc Language Support for VS Code

This extension adds syntax highlighting and language support for the Arc programming language in Visual Studio Code.

## Features

- **Syntax Highlighting**: Recognizes and colorizes Arc language constructs
  - Keywords (types, control flow, declarations)
  - Strings and escape sequences
  - Numbers (integers and floats)
  - Comments (line and block)
  - Operators and punctuation

- **Smart Indentation**: Automatic indentation for braces and control structures

- **Bracket Matching**: Auto-completion and matching of brackets, braces, and quotes

## Installation

### From VS Code Marketplace (Once Published)

Search for "Arc Language Support" in the VS Code Extensions marketplace.

### Manual Installation

1. Copy the `vscode-ext` folder to your VS Code extensions directory:
   - **Linux/Mac**: `~/.vscode/extensions/`
   - **Windows**: `%USERPROFILE%\.vscode\extensions\`

2. Rename the folder to `arc-language-<version>`

3. Restart VS Code

### Development Installation

```bash
cd vscode-ext
npm install  # if dependencies are added later
code --install-extension . 
```

## Supported File Types

- `.arc` - Arc source code files

## Syntax Highlighting Categories

| Category | Color (Light Theme) | Examples |
|----------|---|----------|
| Keywords (Type) | Blue | `int`, `float`, `bool`, `void` |
| Keywords (Control) | Orange | `if`, `else`, `for`, `while`, `return` |
| Keywords (Declaration) | Purple | `struct`, `enum`, `fn` |
| Keywords (Constant) | Green | `true`, `false` |
| Strings | Red | `"hello"`, `'world'` |
| Numbers | Cyan | `42`, `3.14` |
| Comments | Gray | `// comment`, `/* block */` |
| Operators | White | `+`, `-`, `==`, `&&`, etc. |

## Configuration

The extension uses VS Code's built-in theming system. Colors are determined by your selected VS Code theme.

To customize colors, add rules to your VS Code `settings.json`:

```json
"editor.tokenColorCustomizations": {
  "textMateRules": [
    {
      "scope": "keyword.type.arc",
      "settings": {
        "foreground": "#0000FF"
      }
    }
  ]
}
```

## Contributing

Report issues or suggest improvements on the [Arc GitHub repository](https://github.com/ital87/arc).

## License

MIT License - See the main Arc repository for details.

