# phpc 使用说明书

> `phpc` 是 OpKit 的 CLI 编译工具，用于将 PHP 源代码编译为 `.phpc` 二进制 Opcode 缓存文件，并支持 Phar 打包、静态分析、Stub 生成等高级功能。

---

## 目录

1. [快速开始](#快速开始)
2. [基础用法](#基础用法)
3. [编译模式](#编译模式)
4. [增量与强制编译](#增量与强制编译)
5. [多源路径与通配符](#多源路径与通配符)
6. [配置文件](#配置文件)
7. [查看 .phpc 信息](#查看-phpc-信息)
8. [生成入口文件](#生成入口文件)
9. [静态分析](#静态分析)
10. [生成 PHP Stubs](#生成-php-stubs)
11. [Phar 打包](#phar-打包)
12. [错误处理与常见问题](#错误处理与常见问题)
13. [完整参数参考](#完整参数参考)

---

## 快速开始

### 前提条件

- PHP 8.2 - 8.5
- OpKit 扩展已作为 `zend_extension` 加载
- `phar.readonly=Off`（如需打包 Phar）

### 最简编译示例

```bash
php -d zend_extension=modules/opkit.so ./bin/phpc -s src/ -o dist/
```

编译后会在 `dist/` 目录下生成对应的 `.phpc` 文件和 `entry.php` 入口文件：

```
dist/
  main.phpc
  lib/utils.phpc
  entry.php
```

### 运行编译结果

```bash
php -d zend_extension=modules/opkit.so dist/entry.php
```

---

## 基础用法

### 编译单个文件

```bash
php phpc -s index.php -o output/
```

输出：`output/index.phpc`、`output/entry.php`

### 编译整个目录（递归）

```bash
php phpc -s src/ -o dist/
```

自动递归编译 `src/` 下所有 `.php` 文件，保持目录结构输出到 `dist/`。

### 使用长参数

```bash
php phpc --src src/ --output dist/
```

---

## 编译模式

### 1. 仅编译

```bash
php phpc -s src/ -o dist/
```

### 2. 编译 + 自动生成 entry.php

默认行为。编译完成后，如果在输出目录中检测到 `.phpc` 文件且没有使用 `-p` 参数，会自动生成 `entry.php`：

```php
<?php
opkit_load_multi([
    __DIR__ . '/main.phpc',
    __DIR__ . '/lib/utils.phpc',
]);
exit(opkit_boot());
```

### 3. 仅生成 entry.php（不编译）

```bash
php phpc -e dist/entry.php -o dist/
```

根据输出目录中已有的 `.phpc` 文件生成入口文件。

### 4. 编译 + Phar 打包一体化

```bash
php phpc -s src/ -o dist/ -p app.phar
```

编译源码到 `dist/`，然后打包为 `app.phar`，并自动包含 `entry.php`。

### 5. 仅打包已有目录

```bash
php phpc -o dist/ -p app.phar
```

不编译，直接将 `dist/` 下的所有文件（含 `.phpc` 和 `entry.php`）打包为 Phar。

---

## 增量与强制编译

### 增量编译（默认开启）

phpc 默认启用增量编译。对于每个源文件，如果满足以下条件则跳过：

1. 目标 `.phpc` 文件已存在
2. 源文件修改时间 `mtime` <= 目标文件 `mtime`
3. 目标文件的 `system_id` 与当前 PHP 环境匹配

```bash
php phpc -s src/ -o dist/
# 第二次运行会自动跳过未变更的文件
```

### 强制重新编译

使用 `-f` 强制编译所有文件，忽略增量检查：

```bash
php phpc -s src/ -o dist/ -f
```

### 禁用增量编译

使用 `--no-incremental` 禁用增量机制，每次运行都会编译所有文件：

```bash
php phpc -s src/ -o dist/ --no-incremental
```

---

## 多源路径与通配符

### 多源路径

支持同时指定多个源路径：

```bash
php phpc -s src/ -s vendor/lib/ -o dist/
```

### glob 通配符

支持 shell glob 通配符：

```bash
php phpc -s "src/lib/*.php" -o dist/
php phpc -s "modules/*" -o dist/
```

> 注意：包含通配符的路径建议加引号，防止 shell 提前展开。

---

## 配置文件

phpc 支持通过 `opkit.json` 配置文件管理编译参数，CLI 参数优先级高于配置文件。

### 配置文件示例

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

### 使用配置文件

```bash
php phpc -c opkit.json
```

### 配置项说明

| 配置项 | 类型 | 说明 |
|--------|------|------|
| `src` | string / array | 源文件或目录路径（相对于配置文件目录） |
| `output` | string | 输出目录 |
| `exclude` | array | glob 排除模式 |
| `force` | bool | 强制重新编译 |
| `no-incremental` | bool | 禁用增量编译 |
| `phar` | string | 生成的 Phar 文件名 |
| `compress` | string | Phar 压缩：`gz`、`bz2`、`none` |
| `sign` | string | Phar 签名：`sha1`、`sha256`、`sha512`、`openssl` |
| `sign-key` | string | OpenSSL 签名私钥文件路径 |
| `stubs` | string | 生成 PHP stubs 的输出目录 |
| `entry` | string | 指定 entry.php 生成路径 |

### 排除模式

`exclude` 支持 glob 模式：

```json
{
  "exclude": [
    "vendor/*",
    "**/*Test.php",
    "config/*.php"
  ]
}
```

匹配规则同时作用于完整路径和文件名。

---

## 查看 .phpc 信息

使用 `-i` 或 `--info` 查看已编译 `.phpc` 文件的详细信息：

```bash
php phpc -i dist/main.phpc
```

输出示例：

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

也可以使用子命令形式：

```bash
php phpc info dist/main.phpc
```

---

## 生成入口文件

### 自动生成

编译时如果不指定 `-p`（Phar 模式），且输出目录中有 `.phpc` 文件，会自动生成 `entry.php`：

```bash
php phpc -s src/ -o dist/
# 自动生成 dist/entry.php
```

### 手动指定路径

```bash
php phpc -e bootstrap.php -o dist/
```

生成的 `entry.php` 内容：

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

### 自定义入口逻辑

如需自定义入口逻辑，可以手动调用 API：

```php
<?php
opkit_load_multi([
    __DIR__ . '/main.phpc',
    __DIR__ . '/lib/utils.phpc',
]);

// 注册所有类/函数/常量后手动调用
opkit_boot(null);

// 然后使用已注册的符号
use App\Lib\Calculator;
$calc = new Calculator();
echo $calc->add(2, 3);
```

---

## 静态分析

### 分析目录

```bash
php phpc -a dist/
```

### 分析单文件

```bash
php phpc analyze dist/main.phpc
```

### 输出示例

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

### 冲突检测

如果多个 `.phpc` 文件定义了同名函数/类/常量，会报告冲突：

```
[WARNING] Symbol Conflicts Found:
The following symbols are defined in multiple files:
  - Functions: helper (defined in: lib/a.phpc, lib/b.phpc)
  - Classes: Utils (defined in: core/utils.phpc, vendor/utils.phpc)
```

冲突的符号会导致 `opkit_boot()` 运行时抛出致命错误。

---

## 生成 PHP Stubs

为编译后的 `.phpc` 文件生成 PHP IDE 存根（stub），便于静态分析和 IDE 自动补全。

```bash
php phpc -s src/ -o dist/ --stubs stubs/
```

生成的 stub 文件结构与源码一致，仅包含函数签名、类定义、常量声明，不含实现：

```php
<?php

namespace App\Lib {
    class Calculator {
        public function add(int $a, int $b): int {}
    }
    function helper(): string {}
}
```

### 独立生成 Stubs

如果已有编译输出，可以单独生成：

```bash
php phpc -o dist/ --stubs stubs/
```

---

## Phar 打包

### 基础打包

```bash
php phpc -o dist/ -p app.phar
```

### 编译 + 打包一体化

```bash
php phpc -s src/ -o dist/ -p app.phar
```

打包时会自动在 Phar 中包含 `entry.php`（如果不存在则自动生成）。

### 运行 Phar

```bash
php -d zend_extension=modules/opkit.so app.phar
```

### 压缩

```bash
# GZip 压缩
php phpc -o dist/ -p app.phar -z gz

# BZip2 压缩
php phpc -o dist/ -p app.phar -z bz2

# 不压缩（默认）
php phpc -o dist/ -p app.phar -z none
```

> 需要 PHP 启用对应的 `zlib` 或 `bz2` 扩展。

### 签名

```bash
# SHA1
php phpc -o dist/ -p app.phar --sign sha1

# SHA256
php phpc -o dist/ -p app.phar --sign sha256

# SHA512
php phpc -o dist/ -p app.phar --sign sha512

# OpenSSL（需要私钥）
php phpc -o dist/ -p app.phar --sign openssl --sign-key private.pem
```

### 组合示例

```bash
php phpc -s src/ -o dist/ -p app.phar -z gz --sign sha256
```

编译源码、GZip 压缩、SHA256 签名一体化完成。

---

## 错误处理与常见问题

### "OpKit extension not loaded"

**原因**：PHP 未加载 OpKit 扩展，或使用了 `extension=` 而非 `zend_extension=`。

**解决**：

```bash
php -d zend_extension=/path/to/opkit.so ./bin/phpc ...
```

或在 `php.ini` 中配置：

```ini
zend_extension=opkit.so
```

### "phar.readonly is On"

**原因**：Phar 默认只读，禁止创建/修改。

**解决**：

```bash
php -d phar.readonly=Off ./bin/phpc -o dist/ -p app.phar
```

### "No source files found matching: ..."

**原因**：`-s` 指定的源路径不存在或不匹配任何文件。

**解决**：检查路径是否正确，glob 通配符是否包含匹配的文件。

### System ID 不匹配

`.phpc` 文件包含编译时的 `system_id`（由 PHP 版本 + 架构 + 编译选项决定）。如果运行环境的 `system_id` 与 `.phpc` 不匹配，`opkit_load()` 会加载失败。

```bash
php phpc -i file.phpc
# 查看 System ID Match: NO
```

**解决**：在目标环境重新编译。

### 运行时 "Entry point 'main' not found"

**原因**：`opkit_boot()` 默认查找 `main()` 函数作为入口点，但加载的 `.phpc` 中没有定义。

**解决**：确保源码中定义了 `main()` 函数：

```php
function main(): int {
    echo "Hello\n";
    return 0;
}
```

或使用自定义入口：

```php
opkit_boot('my_entry', ['arg1', 'arg2']);
```

---

## 完整参数参考

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

## 典型工作流

### 开发阶段

```bash
# 1. 编译源码
php -d zend_extension=modules/opkit.so ./bin/phpc -s src/ -o dist/

# 2. 查看编译结果信息
php -d zend_extension=modules/opkit.so ./bin/phpc -i dist/main.phpc

# 3. 运行测试
php -d zend_extension=modules/opkit.so dist/entry.php
```

### 发布阶段

```bash
# 编译 + 生成 stubs + 打包 Phar
php -d zend_extension=modules/opkit.so -d phar.readonly=Off \
  ./bin/phpc -s src/ -o dist/ -p app.phar -z gz --sign sha256 --stubs stubs/

# 运行 Phar
php -d zend_extension=modules/opkit.so app.phar
```

---

*本文档对应 OpKit 版本: v0.0.1-dev*
