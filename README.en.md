# OpKit

OpKit (Opcode Toolkit) is an **experimental** Opcode pre-compilation and persistence extension for PHP. Inspired by the architecture of Zend OPcache, it aims to explore offline compilation and persistent storage solutions for PHP source code.

> **⚠️ Warning**: The project is currently in the **early development and testing stage** (Beta). APIs and binary formats may undergo breaking changes in the future. DO NOT use it in critical production environments.

## 🚀 Core Features

### 1. Deep Pre-compilation
- **Full Persistence**: Pre-compiles PHP source code into binary `.phpc` files, covering OpCodes, strings, constants, functions, and class metadata.
- **Skip Lexical/Syntax Analysis**: Directly maps memory during loading, skipping all compilation stages, aiming to reduce CPU consumption and startup latency.
- **Symbol Persistence**: Supports functions, namespaced classes defined in scripts, and their associated metadata.
- **Constants Support**: Comprehensive cross-request persistence for `define()`, global `const`, and class constants.

### 2. Automated Build Tool (phpc)
- **Incremental Compilation**: Intelligently compares source modification times, PHP environment System ID, and binary file Magic value, recompiling only changed or incompatible files, improving build efficiency for large projects.
- **Configuration-driven**: Supports `opkit.json` for managing tasks, with support for parameter inheritance and overrides.
- **Concurrency Safety**: Uses atomic writes (temporary files + atomic renaming) and exclusive locks to improve the reliability of the build process.
- **Performance Profiling**: Built-in profiling feature providing file-level compilation time statistics.
- **Static Analysis Support**: Extracts detailed class and function metadata (including visibility, static flags, and type signatures), and supports generating PHP stubs via `--stubs` for recognition by IDEs and static analysis tools.
- **Phar Support**: Automatically fixes `__FILE__` and `__DIR__` paths in Phar archives, making it easier to integrate with packaging workflows.

### 3. Runtime Environment and Analysis
- **Shadow Partition Statistics**: Provides memory usage analysis for four partitions: Metadata, Code, Data, and Misc, displaying resource consumption.
- **Batch Loading**: Mount all compilation products of a project at once via `opkit_load_multi`.
- **Flexible Bootstrapping**: `opkit_boot` supports custom entry functions (default is `main`) and dynamic parameter passing.
- **Error Tolerance**: Automatically skips erroneous files during batch compilation and provides ParseError line numbers and cause identification.

## 📋 Requirements

- **PHP Version**: Supports PHP 8.2 and 8.3.
- **Build Tools**: Requires `phpize`, `php-config`, `make`, and a C compiler (e.g., `gcc`).
- **Runtime Dependency**: OpKit must be loaded as a **Zend Extension** and is incompatible with `Zend OPcache`.

---

## 🛠️ Compilation and Installation

As a standalone PHP extension, you can compile it using the standard `phpize` method:

```bash
phpize
./configure
make
sudo make install
```

### Loading Method
OpKit must be loaded as a **Zend Extension**. Add to your `php.ini`:

```ini
zend_extension=opkit.so
```

> **⚠️ Note**: OpKit has an underlying mechanism conflict with `Zend OPcache`, and **both cannot be enabled simultaneously**. Ensure `extension=opcache.so` or `zend_extension=opcache.so` is disabled.

---

## 📖 Quick Start

### 1. Compile Project
Use the built-in `phpc` tool to compile all PHP files from the `src/` directory to the `dist/` directory:

```bash
php -d zend_extension=opkit.so bin/phpc -s src/ -o dist/
```

### 2. Run Application
`phpc` automatically generates an `entry.php` bootstrap file in the output directory. Execute it directly:

```bash
php -d zend_extension=opkit.so dist/entry.php
```

---

## 💻 Command Line Tool (phpc)

`phpc` is the core build assistance script for OpKit, located at `bin/phpc`.

### Common Command Examples

| Scenario | Command |
| :--- | :--- |
| **Basic Compilation** | `phpc -s src/ -o dist/` |
| **Using Config** | `phpc -c opkit.json` |
| **Force Recompile** | `phpc -s src/ -o dist/ -f` |
| **Static Analysis**| `phpc analyze dist/` |
| **Package Phar** | `phpc -s src/ -o dist/ -p app.phar` |
| **View Details** | `phpc -i dist/User.phpc` |

### Parameter Details

- `-s, --src <dir|file>`: Source path.
- `-o, --output <dir>`: Target output directory.
- `-c, --config <file>`: Specify JSON configuration file (defaults to `opkit.json` in the current directory).
- `-p, --phar <name>`: Automatically package products into a Phar archive after compilation.
- `-z, --compress <gz|bz2|none>`: Specify Phar compression format (default `none`).
- `--sign <sha1|sha256|sha512|openssl>`: Specify Phar digital signature algorithm (default `sha1`).
- `--sign-key <file>`: Specify the private key file path for OpenSSL signature.
- `-e, --entry <path>`: Specify the filename of the generated bootstrap entry (default `entry.php`).
- `-f, --force`: Disable incremental mode and force recompile all files.
- `--no-incremental`: Explicitly disable incremental compilation mode.
- `-a, --analyze <dir|file>`: Perform static analysis on compiled `.phpc` files, summarizing symbol tables (functions, classes, constants) and checking for definition conflicts across files.
- `--stubs <dir>`: Generate PHP stubs for compilation products, solving the issue of binary files not being recognized by static analysis tools (e.g., PHPStan, Psalm, IDEs).
- `-i, --info <file>`: Interactively view symbol tables, function signatures, class attributes, and memory logical partition statistics of a `.phpc` file.

---

## ⚙️ Configuration File (opkit.json)

Creating an `opkit.json` in the project root is recommended to simplify the build process:

```json
{
    "src": "src/",
    "output": "dist/",
    "phar": "release/app.phar",
    "compress": "gz",
    "sign": "sha256",
    "entry": "loader.php",
    "force": false
}
```

---

## 📦 Composer Plugin

OpKit provides an official Composer plugin that can automatically trigger the compilation process after executing `composer install` or `composer update`.

### 1. Install Plugin
Require the plugin in your project (ensure OpKit extension is loaded):

```bash
composer require zymphp/opkit
```

### 2. Configuration and Auto-compilation
The plugin automatically looks for `opkit.json` in the project root for compilation. You only need to configure this file in `composer.json`, and all subsequent installation/update operations will automatically update the compilation products.

Additionally, the plugin provides the following commands for managing the extension from source:

```bash
# Build the extension
composer opkit-build

# Install the extension (supports automatic sudo and password prompt)
composer opkit-install

# Clean build artifacts
composer opkit-clean
```

> **Tip**: If a command is not found, ensure that `composer install` has been executed. These commands automatically detect and prioritize the use of build tools that match the PHP version running `composer`.

---

## 🔍 IDE Support and Stubs

Since the OpKit API is provided by a C extension, IDEs (like PHPStorm, VSCode) cannot recognize these functions by default. To achieve a perfect auto-completion and static analysis experience, OpKit provides two stub solutions:

### 1. Extension API Stubs (Built-in)
When installing `zymphp/opkit` via Composer, the built-in stub files of the extension are automatically included in the project. IDEs will recognize core APIs like `opkit_boot` and `opkit_load`.

### 2. Business Code Stubs (Auto-generated)
For the `.phpc` binary files you compile, you can use the `phpc --stubs <dir>` command to generate corresponding PHP declaration files for them. This allows static analysis tools (like PHPStan) to process compiled projects just like source code.

---

## 📚 API Reference

### Compilation Interface
- `opkit_compile_file(string $output_path, string $filename): bool`
  Compiles a single file. `$output_path` is the target directory.
- `opkit_compile_dir(string $output_path, string $dir): bool`
  Recursively compiles all `.php` files in a directory.
- `opkit_gen_entry_file(string $output_path): bool`
  Generates `entry.php` in the target directory, including batch loading and bootstrapping logic.

### Loading and Execution
- `opkit_load(string $filename): bool`
  Loads a single `.phpc` file into persistent memory.
- `opkit_load_multi(array $filenames): void`
  Batch loads files, more efficient.
- `opkit_boot(callable|string|null $entry = "main", array $args = []): mixed`
  1. Registers symbols (classes, functions, constants) from all loaded scripts.
  2. Executes top-level instructions (e.g., `define`) of each script.
  3. Calls the entry function specified by `$entry` (defaults to global `main`).
  4. Returns the return value of the entry function. Throws exceptions if no script is loaded, the entry point is not found, or the call fails.

### Debugging and Analysis
- `opkit_get_info(string $filename): ?array`
  Extracts metadata from a binary file.

---

## 📜 Credits

- **Author**: Eno-CN <Eno_CN@qq.com>
- **Assistant**: Developed with help from AI Assistant - Junie.
- **Reference**: OpKit is heavily based on [Zend OPcache](https://github.com/php/php-src/tree/master/ext/opcache).
- **Acknowledgment**: This product includes PHP software, freely available from <http://www.php.net/software/>.
