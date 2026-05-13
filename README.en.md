# OpKit

OpKit (Opcode Toolkit) is an **experimental** Opcode pre-compilation and persistence extension for PHP. Inspired by the architecture of Zend OPcache, it aims to explore offline compilation and persistent storage solutions for PHP source code.

> **⚠️ Warning**: The project is currently in the **early development and testing stage** (Beta). APIs and binary formats may undergo breaking changes in the future. DO NOT use it in critical production environments. Issues and Pull Requests are welcome.

## 🚀 Core Features

### 1. Deep Pre-compilation
- **Full Persistence**: Pre-compiles PHP source code into binary `.phpc` files, covering OpCodes, strings, constants, functions, and class metadata.
- **Skip Lexical/Syntax Analysis**: Directly maps memory during loading, skipping all compilation stages, aiming to reduce CPU consumption and startup latency.
- **Symbol Persistence**: Supports functions, namespaced classes defined in scripts, and their associated metadata.
- **Constants Support**: Comprehensive cross-request persistence for `define()`, global `const`, and class constants.

### 2. Automated Build Tool (phpc)
- **Incremental Compilation**: Automatically compares source modification times, PHP environment System ID, and binary file Magic value, recompiling only changed or incompatible files, improving build efficiency for large projects.
- **Configuration-driven**: Supports `opkit.json` for managing tasks, with support for parameter inheritance and overrides.
- **Concurrency Safety**: Uses atomic writes (temporary files + atomic renaming) and exclusive locks to improve the reliability of the build process.
- **Performance Profiling**: Built-in profiling feature providing file-level compilation time statistics.
- **Static Analysis Support**: Extracts detailed class and function metadata (including visibility, static flags, and type signatures), and supports generating PHP stubs via `--stubs` for recognition by IDEs and static analysis tools.
- **Phar Support**: Automatically fixes `__FILE__` and `__DIR__` paths in Phar archives, making it easier to integrate with packaging workflows.

### 3. Runtime Environment and Analysis
- **Shadow Partition Statistics**: Provides memory usage analysis for four partitions: Metadata, Code, Data, and Misc, displaying resource consumption.
- **Shared Memory Cache**: Supports caching script data into inter-process shared memory via `mmap(MAP_SHARED)`, enabling zero-copy sharing across multiple processes (e.g., PHP-FPM Workers) and significantly reducing memory footprint and startup latency.
- **SHM Management**: Provides `opkit_shm_reset()` to clear shared memory cache and `opkit_shm_stat()` for real-time shared memory usage statistics.
- **Batch Loading**: Mount all compilation products of a project at once via `opkit_load_multi`.
- **Flexible Bootstrapping**: `opkit_boot` supports custom entry functions (default is `main`) and dynamic parameter passing.
- **Error Tolerance**: Automatically skips erroneous files during batch compilation and provides ParseError line numbers and cause identification.

## 📋 Requirements

- **PHP Version**: Supports PHP 8.2, 8.3, 8.4, and 8.5.
- **Build Tools**: Requires `phpize`, `php-config`, `make`, and a C compiler (e.g., `gcc`).
- **Runtime Dependency**: OpKit must be loaded as a **Zend Extension**. Coexists with Zend OPcache via deep integration.

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

> **⚠️ Note**: OpKit coexists with Zend OPcache via deep integration.

### Configuration

INI entries registered by OpKit itself:

| Configuration | Type | Default | Scope | Description |
|---------------|------|---------|-------|-------------|
| `opkit.shm_size` | `zend_long` (bytes) | `0` | `PHP_INI_SYSTEM` | Total size of the shared memory segment. `0` disables shared memory and uses process-private heap memory. Values greater than 0 allocate inter-process shared memory via `mmap(MAP_SHARED\|MAP_ANONYMOUS)` for caching `.phpc` script data. |

Related external PHP INI entries that affect OpKit:

| Configuration | Source | Description |
|---------------|--------|-------------|
| `zend_extension=opkit.so` | PHP Core | **Must** be loaded as a Zend Extension. Cannot be written as `extension=opkit.so`. |
| `phar.readonly=Off` | Phar extension | Only required when **creating** Phar archives (e.g. `phpc -p`). Not needed for running `.phpc` files. |

**Configuration examples**:

```ini
; Default: disable shared memory (recommended for PHP-FPM)
zend_extension=opkit.so
opkit.shm_size=0

; Enable 32MB shared memory (recommended for long-running servers)
zend_extension=opkit.so
opkit.shm_size=33554432
```

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

`phpc` is the core build script for OpKit, located at `bin/phpc`.

### Common Command Examples

| Scenario | Command |
| :--- | :--- |
| **Basic Compilation** | `phpc -s src/ -o dist/` |
| **Using Config** | `phpc -c opkit.json` |
| **Force Recompile** | `phpc -s src/ -o dist/ -f` |
| **Static Analysis** | `phpc analyze <dir\|file>` |
| **Package Phar** | `phpc -s src/ -o dist/ -p app.phar` |
| **View Details** | `phpc -i dist/User.phpc` |

> 📖 **Full manual**: Please refer to [`PHPC_MANUAL.md`](PHPC_MANUAL.md) for incremental compilation, multi-source paths, glob wildcards, Phar compression & signing, static analysis, stub generation, and more.

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

Since the OpKit API is provided by a C extension, IDEs (like PHPStorm, VSCode) cannot recognize these functions by default. To get good auto-completion and static analysis support, OpKit provides two stub solutions:

### 1. Extension API Stubs (Built-in)
When installing `zymphp/opkit` via Composer, the built-in stub files of the extension are automatically included in the project. IDEs will recognize core APIs like `opkit_boot` and `opkit_load`.

### 2. Project Code Stubs (Auto-generated)
For the `.phpc` binary files you compile, you can use the `phpc --stubs <dir>` command to generate corresponding PHP declaration files for them. This allows static analysis tools (like PHPStan) to process compiled projects just like source code.

---

## 📚 Documentation

OpKit provides the following technical documentation to help you understand the system implementation:

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture overview, including module responsibilities, data structures, memory management, and PHP version compatibility |
| [PHPC_FILE_FORMAT.md](docs/PHPC_FILE_FORMAT.md) | `.phpc` file format specification, detailing file header structure, serialization mechanism, and deserialization process |
| [COMPILATION_PROCESS.md](docs/COMPILATION_PROCESS.md) | Compilation process details, covering complete compile-time and runtime workflows |
| [ZEND_COMPILE_OPTIONS.md](docs/ZEND_COMPILE_OPTIONS.md) | Zend compile options reference for debugging and optimization |

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
- `opkit_boot(callable|string|null $entry = "main", array $args = []): int`
  1. Registers symbols (classes, functions, constants) from all loaded scripts.
  2. Executes top-level instructions (e.g., `define`) of each script.
  3. Calls the entry function specified by `$entry` (defaults to global `main`).
  4. Returns the return value of the entry function (returns directly only when the return value is `int`, otherwise returns 0). Throws exceptions if no script is loaded, the entry point is not found, or the call fails.
- `opkit_is_loaded(string $filename): bool`
  Checks whether the specified `.phpc` file has already been loaded.

### Debugging and Analysis
- `opkit_get_info(string $filename): ?array`
  Extracts metadata from a binary file.

### Shared Memory Management
- `opkit_shm_reset(): bool`
  Resets the shared memory allocator, clearing all cached script data. Automatically cleans up currently registered symbols before reset to prevent dangling pointers.
- `opkit_shm_stat(): ?array`
  Returns shared memory statistics including `shm_size` (total capacity), `used` (bytes used), and `free` (bytes remaining). Returns `null` if shared memory is not enabled.

---

## 👤 Author

Eno-CN <Eno_CN@qq.com>

## 📜 Credits

- **Assistant**: Developed with assistance from AI Assistants OpenCode and Junie. Models used: Deepseek v4 pro, Kimi K2.6, and others.
- **Reference**: OpKit is heavily based on [Zend OPcache](https://github.com/php/php-src/tree/master/ext/opcache).
- **Acknowledgment**: This product includes PHP software, freely available from <http://www.php.net/software/>.
