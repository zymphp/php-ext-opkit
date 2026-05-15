# OpKit

OpKit (Opcode Toolkit) 是一个处于 **实验阶段** 的 PHP Opcode 预编译与持久化扩展。它参考了 Zend OPcache 的底层架构，旨在探索 PHP 源码的离线编译与持久化机制。

> **⚠️ 声明**：目前该项目仍处于 **早期开发与测试阶段**，其 API 和二进制格式在未来可能会发生非兼容性变更，请勿直接用于关键生产环境。欢迎试用、提交 Issue 与 Pull Request 参与贡献。

## 🚀 核心特性

### 1. 深度预编译
- **全量持久化**: 将 PHP 源码预编译为二进制 `.phpc` 文件，涵盖 OpCodes、字符串、常量、函数及类元数据。
- **跳过词法/语法分析**: 加载时直接映射内存，跳过所有编译阶段，旨在降低 CPU 消耗和启动延迟。
- **符号持久化**: 支持脚本中定义的函数、命名空间类及其关联的元数据。
- **常量支持**: 覆盖 `define()`、全局 `const` 以及类常量的跨请求持久化。

### 2. 自动化构建工具 (phpc)
- **增量编译**: 自动比对源码修改时间、PHP 环境 System ID 以及二进制文件 Magic 值，仅重编译变动或不兼容的文件，提升大型项目构建效率。
- **配置驱动**: 支持 `opkit.json` 管理任务，支持参数继承与覆盖。
- **并发安全**: 采用原子性写入（临时文件 + 原子重命名）和排他锁，提高构建过程的可靠性。
- **性能分析**: 内置 Profiling 功能，提供文件级编译耗时统计。
- **静态分析支持**: 提取详细的类、函数元数据（含可见性、静态标志及类型签名），并支持通过 `--stubs` 生成 PHP 定义存根，便于 IDE 和静态分析工具识别。
- **Phar 支持**: 自动修复 Phar 归档中的 `__FILE__` 与 `__DIR__` 路径，方便集成打包流程。

### 3. 运行环境与分析
- **影子分区统计**: 提供 Metadata、Code、Data、Misc 四个分区的内存占用分析，展示资源消耗情况。
- **共享内存缓存**: 支持通过 `mmap(MAP_SHARED)` 将脚本数据缓存到进程间共享内存，实现多进程间（如 PHP-FPM Worker）的零拷贝共享，显著降低内存占用和启动延迟。
- **SHM 管理**: 提供 `opkit_shm_reset()` 一键清空共享内存缓存，`opkit_shm_stat()` 实时查看共享内存使用统计。
- **批量加载**: 通过 `opkit_load_multi` 挂载项目的编译产物。
- **灵活引导**: `opkit_boot` 支持自定义入口函数（默认为 `main`）并支持参数传递。
- **错误容忍**: 在批量编译中自动跳过错误文件，并提供 ParseError 行号和原因定位。

## 📋 环境要求

- **PHP 版本**: 支持 PHP 8.2、8.3、8.4 和 8.5。
- **构建工具**: 需要安装 `phpize`、`php-config`、`make` 以及 C 编译器（如 `gcc`）。
- **运行依赖**: OpKit 必须作为 **Zend Extension** 加载。通过深度集成可与 Zend OPcache 透明共存。

---

## 🛠️ 编译安装

作为独立的 PHP 扩展，你可以使用标准的 `phpize` 方式进行编译：

```bash
phpize
./configure
make
sudo make install
```

### 加载方式
OpKit 必须作为 **Zend Extension** 加载。在 `php.ini` 中添加：

```ini
zend_extension=opkit.so
```

> **⚠️ 注意**：OpKit 通过深度集成可与 Zend OPcache 静默共存。
>
> **💡 推荐**：`phpc` 编译时使用 `-n`（不加载 php.ini 其他扩展）隔离运行，避免 swoole/curl 等扩展与 opkit 产生内存冲突：
> ```bash
> php -n -d zend_extension=opkit.so bin/phpc -s src/ -o dist/
> ```

### 配置项

OpKit 自身注册的 INI 配置项：

| 配置项 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `opkit.shm_size` | `zend_long`（字节） | `0` | `PHP_INI_SYSTEM` | 共享内存段总容量。`0` 表示禁用共享内存，使用进程私有堆内存；大于 0 时通过 `mmap(MAP_SHARED\|MAP_ANONYMOUS)` 分配进程间共享内存，用于缓存 `.phpc` 脚本数据。 |

相关的外部 PHP 配置项（影响 OpKit 运行）：

| 配置项 | 来源 | 说明 |
|--------|------|------|
| `zend_extension=opkit.so` | PHP Core | **必须**作为 Zend Extension 加载。不可写成 `extension=opkit.so`。 |
| `phar.readonly=Off` | Phar 扩展 | 仅在**创建** Phar 包时需要（如 `phpc -p`）。运行 `.phpc` 文件时不需要。 |

**配置示例**：

```ini
; 默认配置：禁用共享内存（推荐 PHP-FPM 使用）
zend_extension=opkit.so
opkit.shm_size=0

; 开启 32MB 共享内存（推荐常驻内存模型使用）
zend_extension=opkit.so
opkit.shm_size=33554432
```

---

## 📖 快速入门

### 1. 编译项目
使用内置的 `phpc` 工具将 `src/` 目录下的所有 PHP 文件编译到 `dist/` 目录：

```bash
php -n -d zend_extension=opkit.so bin/phpc -s src/ -o dist/
```

### 2. 运行应用
`phpc` 会自动在输出目录生成 `entry.php` 引导文件。直接执行即可：

```bash
php -d zend_extension=opkit.so dist/entry.php
```

---

## 💻 命令行工具 (phpc)

`phpc` 是 OpKit 的核心构建辅助脚本，位于 `bin/phpc`。

### 常用命令示例

| 场景 | 命令 |
| :--- | :--- |
| **基础编译** | `php -n -d zend_extension=opkit.so bin/phpc -s src/ -o dist/` |
| **使用配置** | `php -n -d zend_extension=opkit.so bin/phpc -c opkit.json` |
| **强制重编** | `php -n -d zend_extension=opkit.so bin/phpc -s src/ -o dist/ -f` |
| **静态分析** | `php -n -d zend_extension=opkit.so bin/phpc analyze <dir\|file>` |
| **打包 Phar** | `php -n -d zend_extension=opkit.so bin/phpc -s src/ -o dist/ -p app.phar` |
| **查看详情** | `php -n -d zend_extension=opkit.so bin/phpc -i dist/User.phpc` |

> 📖 **完整使用说明**：请参阅 [`PHPC_MANUAL.md`](PHPC_MANUAL.md)，包含增量编译、多源路径、glob 通配符、Phar 压缩签名、静态分析、Stub 生成等详细用法。

---

## ⚙️ 配置文件 (opkit.json)

推荐在项目根目录创建 `opkit.json` 以简化构建流程：

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

## 📦 Composer 插件

OpKit 提供官方 Composer 插件，可在执行 `composer install` 或 `composer update` 后自动触发编译流程。

### 1. 安装插件
在项目中要求该插件（需确保已加载 OpKit 扩展）：

```bash
composer require zymphp/opkit
```

### 2. 配置与自动编译
插件会自动寻找项目根目录下的 `opkit.json` 进行编译。只需确保该文件已正确配置，后续的所有安装/更新操作都会自动更新编译产物。

此外，该插件还提供了以下命令，用于从源码管理扩展：

```bash
# 编译扩展
composer opkit-build

# 安装扩展 (支持自动调用 sudo 并提示输入密码)
composer opkit-install

# 清理编译产物
composer opkit-clean
```

> **提示**：如果命令未找到，请确保已执行 `composer install`。这些命令会自动探测并优先使用与运行 `composer` 的 PHP 版本相匹配的构建工具。

---

## 🔍 IDE 支持与存根 (Stubs)

由于 OpKit 的 API 由 C 扩展提供，IDE（如 PHPStorm、VSCode）默认无法识别这些函数。为获得良好的自动补全与静态分析体验，OpKit 提供了两种存根方案：

### 1. 扩展 API 存根 (内置)
当通过 Composer 安装 `zymphp/opkit` 后，扩展自带的存根文件会自动包含在项目中。IDE 将能够识别 `opkit_boot`、`opkit_load` 等核心 API。

### 2. 业务代码存根 (自动生成)
对于你编译出的 `.phpc` 二进制文件，可以使用 `phpc --stubs <dir>` 命令为它们生成对应的 PHP 声明文件。这使得静态分析工具（如 PHPStan）能够像处理源码一样处理编译后的项目。

---

## 📚 文档

OpKit 提供以下技术文档，帮助深入了解系统实现：

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | 系统架构概览，包含各模块职责、数据结构、内存管理和 PHP 版本兼容性说明 |
| [PHPC_FILE_FORMAT.md](docs/PHPC_FILE_FORMAT.md) | `.phpc` 文件格式规范，详细描述文件头结构、序列化机制和反序列化流程 |
| [COMPILATION_PROCESS.md](docs/COMPILATION_PROCESS.md) | 编译流程详解，包含编译时和运行时的完整流程 |
| [ZEND_COMPILE_OPTIONS.md](docs/ZEND_COMPILE_OPTIONS.md) | Zend 编译选项参考，用于调试和优化 |

---

## 📚 API 参考

### 编译接口
- `opkit_compile_file(string $output_path, string $filename): bool`
  编译单个文件。`$output_path` 为目标目录。
- `opkit_compile_dir(string $output_path, string $dir): bool`
  递归编译目录下所有 `.php` 文件。
- `opkit_gen_entry_file(string $output_path): bool`
  在目标目录生成 `entry.php`，内含批量加载与引导逻辑。

### 加载与执行
- `opkit_load(string $filename): bool`
  加载单个 `.phpc` 文件到持久化内存。
- `opkit_load_multi(array $filenames): void`
  批量加载文件，效率更高。
- `opkit_boot(callable|string|null $entry = "main", array $args = []): int`
  1. 注册所有已加载脚本的符号（类、函数、常量）。
  2. 执行各脚本的顶层指令（如 `define`）。
  3. 调用 `$entry` 指定的入口函数（默认为全局 `main`）。
  4. 返回入口函数的返回值（仅当返回值为 `int` 时直接返回，否则返回 0）。若未加载脚本、找不到入口函数或调用失败，将抛出异常。
- `opkit_is_loaded(string $filename): bool`
  检查指定的 `.phpc` 文件是否已被加载。

### 调试与分析
- `opkit_get_info(string $filename): ?array`
  提取二进制文件的元数据。

### 共享内存管理
- `opkit_shm_reset(): bool`
  重置共享内存分配器，清空所有缓存的脚本数据。调用前会自动清理当前进程已注册的符号，防止悬空指针。
- `opkit_shm_stat(): ?array`
  获取共享内存统计信息，返回包含 `shm_size`（总容量）、`used`（已使用）和 `free`（剩余）的数组。若未启用共享内存则返回 `null`。

---

## 👤 作者

Eno-CN <Eno_CN@qq.com>

## 📜 致谢

- **AI 助手**：在 OpenCode、Junie 等 AI 助手的协助下完成开发。使用模型包括 Deepseek v4 pro、Kimi K2.6 等。
- **参考**：OpKit 大量参考并基于 [Zend OPcache](https://github.com/php/php-src/tree/master/ext/opcache) 实现。
- **声明**：本产品包含 PHP 软件，可自由从 <http://www.php.net/software/> 获取。
