# phpc User Manual

> `phpc` is the CLI compilation tool for OpKit, used to compile PHP source code into `.phpc` binary Opcode cache files, with support for Phar packaging, static analysis, stub generation, and other advanced features.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Basic Usage](#basic-usage)
3. [Compilation Modes](#compilation-modes)
4. [Incremental and Force Compilation](#incremental-and-force-compilation)
5. [Multi-Source Paths and Wildcards](#multi-source-paths-and-wildcards)
6. [Configuration File](#configuration-file)
7. [Viewing .phpc Info](#viewing-phpc-info)
8. [Generating Entry Files](#generating-entry-files)
9. [Static Analysis](#static-analysis)
10. [Generating PHP Stubs](#generating-php-stubs)
11. [Phar Packaging](#phar-packaging)
12. [Error Handling and FAQ](#error-handling-and-faq)
13. [Complete Parameter Reference](#complete-parameter-reference)

---

## Quick Start

### Prerequisites

- PHP 8.2 - 8.5
- OpKit extension loaded as `zend_extension`
- `phar.readonly=Off` (if Phar packaging is needed)

### Simplest Compilation Example

```bash
php -d zend_extension=modules/opkit.so ./bin/phpc -s src/ -o dist/
```

After compilation, the `dist/` directory will contain corresponding `.phpc` files and an `entry.php` entry file:

```
dist/
  main.phpc
  lib/utils.phpc
  entry.php
```

### Running the Compiled Result

```bash
php -d zend_extension=modules/opkit.so dist/entry.php
```

---

## Basic Usage

### Compile a Single File

```bash
php phpc -s index.php -o output/
```

Output: `output/index.phpc`, `output/entry.php`

### Compile an Entire Directory (Recursive)

```bash
php phpc -s src/ -o dist/
```

Automatically and recursively compiles all `.php` files under `src/`, preserving the directory structure in `dist/`.

### Using Long Options

```bash
php phpc --src src/ --output dist/
```

---

## Compilation Modes

### 1. Compile Only

```bash
php phpc -s src/ -o dist/
```

### 2. Compile + Auto-generate entry.php

This is the default behavior. After compilation, if `.phpc` files are detected in the output directory and the `-p` flag is not used, an `entry.php` is automatically generated:

```php
<?php
opkit_load_multi([
    __DIR__ . '/main.phpc',
    __DIR__ . '/lib/utils.phpc',
]);
exit(opkit_boot());
```

### 3. Generate entry.php Only (No Compilation)

```bash
php phpc -e dist/entry.php -o dist/
```

Generates the entry file based on existing `.phpc` files in the output directory.

### 4. Compile + Phar Packaging (All-in-One)

```bash
php phpc -s src/ -o dist/ -p app.phar
```

Compiles the source code to `dist/`, then packages it as `app.phar`, automatically including `entry.php`.

### 5. Package Existing Directory Only

```bash
php phpc -o dist/ -p app.phar
```

Skips compilation and directly packages all files under `dist/` (including `.phpc` and `entry.php`) into a Phar.

---

## Incremental and Force Compilation

### Incremental Compilation (Enabled by Default)

`phpc` enables incremental compilation by default. For each source file, it is skipped if all of the following conditions are met:

1. The target `.phpc` file already exists.
2. The source file modification time `mtime` <= the target file `mtime`.
3. The target file's `system_id` matches the current PHP environment.

```bash
php phpc -s src/ -o dist/
# The second run will automatically skip unchanged files
```

### Force Recompilation

Use `-f` to force compilation of all files, ignoring incremental checks:

```bash
php phpc -s src/ -o dist/ -f
```

### Disable Incremental Compilation

Use `--no-incremental` to disable the incremental mechanism; all files will be compiled on every run:

```bash
php phpc -s src/ -o dist/ --no-incremental
```

---

## Multi-Source Paths and Wildcards

### Multi-Source Paths

Multiple source paths can be specified simultaneously:

```bash
php phpc -s src/ -s vendor/lib/ -o dist/
```

### Glob Wildcards

Shell glob wildcards are supported:

```bash
php phpc -s "src/lib/*.php" -o dist/
php phpc -s "modules/*" -o dist/
```

> **Note**: Paths containing wildcards should be quoted to prevent the shell from expanding them prematurely.

---

## Configuration File

`phpc` supports managing compilation parameters via an `opkit.json` configuration file. CLI parameters take priority over configuration file settings.

### Configuration File Example

```json
{
  "src": "src",
  "output": "dist",
  "exclude": [
    "tests/*",
    "vendor/*",
    "*.config.php"
  ],
  "force": false,
  "no-incremental": false,
  "phar": "app.phar",
  "compress": "gz",
  "sign": "sha256",
  "stubs": "stubs/"
}
```

### Using a Configuration File

```bash
php phpc -c opkit.json
```

### Configuration Item Reference

| Key | Type | Description |
|-----|------|-------------|
| `src` | string / array | Source file or directory path (relative to the config file directory) |
| `output` | string | Output directory |
| `exclude` | array | Glob exclusion patterns |
| `force` | bool | Force recompilation |
| `no-incremental` | bool | Disable incremental compilation |
| `phar` | string | Generated Phar file name |
| `compress` | string | Phar compression: `gz`, `bz2`, `none` |
| `sign` | string | Phar signature: `sha1`, `sha256`, `sha512`, `openssl` |
| `sign-key` | string | OpenSSL signature private key file path |
| `stubs` | string | Output directory for generated PHP stubs |
| `entry` | string | Specify the `entry.php` generation path |

### Exclusion Patterns

`exclude` supports glob patterns:

```json
{
  "exclude": [
    "vendor/*",
    "**/*Test.php",
    "config/*.php"
  ]
}
```

Matching rules apply to both the full path and the file name.

---

## Viewing .phpc Info

Use `-i` or `--info` to view detailed information about a compiled `.phpc` file:

```bash
php phpc -i dist/main.phpc
```

Sample output:

```
--------------------------------------------------
OpKit .phpc file information:
File: /path/to/dist/main.phpc
--------------------------------------------------
Magic               : PHPC
System ID           : d4e88a651c5c21e3a7227d0a2333f96d
System ID Match     : YES
Memory Size         : 5,816 bytes
String Size         : 0 bytes
Timestamp           : 2026-05-07 06:32:35
Checksum            : 0x4FFEC54E
--------------------------------------------------
Script Details:
Functions           : 2
Classes             : 1
Early Bindings      : 1
--------------------------------------------------
Logical Subdivisions (Memory Partitioning):
Metadata Area       : 592 bytes
Code Area           : 1,984 bytes
Data Area           : 1,512 bytes
Misc Area           : 1,728 bytes
--------------------------------------------------
Functions List:
  - say_hello(string $name): string
  - main(): int
--------------------------------------------------
Classes List:
  - Class DemoCalc
      * Method: multiply(int $a, int $b): int
--------------------------------------------------
Memory Layout:
Name                           Start          End            Size
Metainfo                       0x00000000     0x00000070     112 bytes
Persistent Script              0x00000070     0x00000288     536 bytes
  Main OpArray                 0x00000078     0x00000178     256 bytes
...
```

You can also use the subcommand form:

```bash
php phpc info dist/main.phpc
```

---

## Generating Entry Files

### Auto-Generation

If `-p` (Phar mode) is not specified and `.phpc` files exist in the output directory, `entry.php` is auto-generated upon compilation:

```bash
php phpc -s src/ -o dist/
# Automatically generates dist/entry.php
```

### Manual Path Specification

```bash
php phpc -e bootstrap.php -o dist/
```

The generated `entry.php` looks like this:

```php
<?php
if (!extension_loaded('opkit')) {
    if (!@dl('opkit.so')) {
        trigger_error('OpKit extension not loaded', E_USER_ERROR);
    }
}

opkit_load_multi([
    __DIR__ . '/main.phpc',
    __DIR__ . '/lib/utils.phpc',
]);

exit(opkit_boot());
```

### Custom Entry Logic

For custom entry logic, you can manually call the API:

```php
<?php
opkit_load_multi([
    __DIR__ . '/main.phpc',
    __DIR__ . '/lib/utils.phpc',
]);

// Register all classes/functions/constants, then call manually
opkit_boot(null);

// Use the registered symbols
use App\Lib\Calculator;
$calc = new Calculator();
echo $calc->add(2, 3);
```

---

## Static Analysis

### Analyze a Directory

```bash
php phpc -a dist/
```

### Analyze a Single File

```bash
php phpc analyze dist/main.phpc
```

### Sample Output

```
--------------------------------------------------
OpKit Static Analysis Report
Target: /path/to/dist
--------------------------------------------------
Total Files     : 3
Total Memory    : 15,432 bytes
Total Symbols   : 5 functions, 2 classes, 0 constants
--------------------------------------------------
Logical Subdivision Summary:
  Metadata Area : 1,200 bytes (7.8%)
  Code Area     : 5,600 bytes (36.3%)
  Data Area     : 4,200 bytes (27.2%)
  Misc Area     : 4,432 bytes (28.7%)
--------------------------------------------------
[SUCCESS] No symbol conflicts detected.
--------------------------------------------------
```

### Conflict Detection

If multiple `.phpc` files define the same function/class/constant name, conflicts will be reported:

```
[WARNING] Symbol Conflicts Found:
The following symbols are defined in multiple files:
  - Functions: helper (defined in: lib/a.phpc, lib/b.phpc)
  - Classes: Utils (defined in: core/utils.phpc, vendor/utils.phpc)
```

Conflicting symbols will cause a fatal error at `opkit_boot()` runtime.

---

## Generating PHP Stubs

Generate PHP IDE stubs (stubs) for compiled `.phpc` files to facilitate static analysis and IDE autocompletion.

```bash
php phpc -s src/ -o dist/ --stubs stubs/
```

The generated stub files mirror the source structure, containing only function signatures, class definitions, and constant declarations, without implementations:

```php
<?php

namespace App\Lib {
    class Calculator {
        public function add(int $a, int $b): int {}
    }
    function helper(): string {}
}
```

### Generating Stubs Independently

If you already have compiled output, you can generate stubs separately:

```bash
php phpc -o dist/ --stubs stubs/
```

---

## Phar Packaging

### Basic Packaging

```bash
php phpc -o dist/ -p app.phar
```

### Compile + Package (All-in-One)

```bash
php phpc -s src/ -o dist/ -p app.phar
```

When packaging, `entry.php` is automatically included in the Phar (auto-generated if it doesn't exist).

### Running the Phar

```bash
php -d zend_extension=modules/opkit.so app.phar
```

### Compression

```bash
# GZip compression
php phpc -o dist/ -p app.phar -z gz

# BZip2 compression
php phpc -o dist/ -p app.phar -z bz2

# No compression (default)
php phpc -o dist/ -p app.phar -z none
```

> Requires the corresponding `zlib` or `bz2` PHP extension.

### Signing

```bash
# SHA1
php phpc -o dist/ -p app.phar --sign sha1

# SHA256
php phpc -o dist/ -p app.phar --sign sha256

# SHA512
php phpc -o dist/ -p app.phar --sign sha512

# OpenSSL (requires a private key)
php phpc -o dist/ -p app.phar --sign openssl --sign-key private.pem
```

### Combined Example

```bash
php phpc -s src/ -o dist/ -p app.phar -z gz --sign sha256
```

Compiles source code, applies GZip compression, and signs with SHA256 — all in one command.

---

## Error Handling and FAQ

### "OpKit extension not loaded"

**Cause**: The OpKit extension is not loaded, or `extension=` was used instead of `zend_extension=`.

**Solution**:

```bash
php -d zend_extension=/path/to/opkit.so ./bin/phpc ...
```

Or configure in `php.ini`:

```ini
zend_extension=opkit.so
```

### "phar.readonly is On"

**Cause**: Phar is read-only by default, preventing creation or modification.

**Solution**:

```bash
php -d phar.readonly=Off ./bin/phpc -o dist/ -p app.phar
```

### "No source files found matching: ..."

**Cause**: The source path specified by `-s` does not exist or does not match any files.

**Solution**: Check that the path is correct and that glob wildcards match files.

### System ID Mismatch

`.phpc` files contain a `system_id` from the compilation environment (derived from PHP version + architecture + compile options). If the runtime environment's `system_id` does not match the `.phpc`'s, `opkit_load()` will fail.

```bash
php phpc -i file.phpc
# Check if System ID Match: NO
```

**Solution**: Recompile in the target environment.

### Runtime "Entry point 'main' not found"

**Cause**: `opkit_boot()` looks for a `main()` function as the default entry point, but none was defined in the loaded `.phpc`.

**Solution**: Ensure the source defines a `main()` function:

```php
function main(): int {
    echo "Hello\n";
    return 0;
}
```

Or use a custom entry point:

```php
opkit_boot('my_entry', ['arg1', 'arg2']);
```

---

## Complete Parameter Reference

```
phpc CLI compilation tool usage:
  php phpc -s <source_path> -o <output_dir>
  php phpc -s <source_path> -o <output_dir> -p <output.phar>
  php phpc -c <config.json>
  php phpc -o <output_dir> -p <output.phar>
  php phpc -e <entry_file_path>
  php phpc -i <.phpc_file_path>
  php phpc -a <dir|file>
  php phpc analyze <dir|file>
  php phpc -s <src> -o <out> --stubs <stubs_dir>

Parameters:
  -s, --src            PHP source file or directory path
                       (supports relative, absolute, and glob wildcards)
                       Can be specified multiple times for multi-path.

  -o, --output         Target output directory

  -p, --phar           Generate Phar archive with specified filename

  -e, --entry          Generate bootstrap entry file at specified path

  -c, --config         Configuration file (default: opkit.json)

  -f, --force          Force recompile all files

  --no-incremental     Disable incremental compilation

  -a, --analyze <dir|file>
                       Perform static analysis on .phpc files

  -z, --compress <format>
                       Phar compression: gz, bz2, none

  --sign <algo>        Phar signature: sha1, sha256, sha512, openssl

  --sign-key <file>    Private key file for OpenSSL signature

  --stubs <dir>        Generate PHP stubs for IDE support

  -i, --info           View detailed info for a .phpc binary file

  -h, --help           Show this help information
```

---

## Typical Workflows

### Development Phase

```bash
# 1. Compile source
php -d zend_extension=modules/opkit.so ./bin/phpc -s src/ -o dist/

# 2. View compilation info
php -d zend_extension=modules/opkit.so ./bin/phpc -i dist/main.phpc

# 3. Run tests
php -d zend_extension=modules/opkit.so dist/entry.php
```

### Release Phase

```bash
# Compile + generate stubs + package Phar
php -d zend_extension=modules/opkit.so -d phar.readonly=Off \
  ./bin/phpc -s src/ -o dist/ -p app.phar -z gz --sign sha256 --stubs stubs/

# Run the Phar
php -d zend_extension=modules/opkit.so app.phar
```

---

*Document corresponds to OpKit version: v0.0.1-dev*
