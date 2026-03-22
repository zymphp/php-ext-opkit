# OpKit 架构文档

## 1. 项目概述

**OpKit** 是一个 PHP Zend 扩展，提供离线 Opcode 预编译和持久化功能。它将 PHP 脚本编译成二进制的 `.phpc` 文件，包含操作码、字符串、常量、函数和类元数据，以便快速加载而无需重新解析。

**核心特性**：
- 基于 Zend OPcache 架构
- 支持 PHP 8.2/8.3/8.4
- 使用影子内存分区（Metadata/Code/Data/Misc）进行持久化存储
- 增量编译支持
- Phar 打包支持

---

## 2. 系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Layer                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   phpc CLI  │  │  Composer   │  │    User PHP Scripts     │  │
│  │   (bin/)    │  │   Plugin    │  │                         │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
└─────────┼────────────────┼─────────────────────┼────────────────┘
          │                │                     │
          ▼                ▼                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    PHP Extension API Layer                      │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  PHP Functions: opkit_compile_file(), opkit_boot()      │    │
│  │  opkit_get_info(), opkit_gen_entry_file()               │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Core C Module Layer                          │
│  ┌──────────────┐ ┌──────────────┐ ┌─────────────────────────┐  │
│  │ opkit_module │ │ opkit_compile│ │  opkit_zend_persist*    │  │
│  │   (生命周期)  │ │  (编译/加载)  │ │    (数据持久化)          │  │
│  └──────────────┘ └──────────────┘ └─────────────────────────┘  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │              opkit_util_funcs (工具函数)                 │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Zend Engine Layer                            │
│         Zend Compiler / Executor / OPcache Codebase             │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 核心模块职责

| 模块 | 文件 | 职责 |
|------|------|------|
| **Module** | `opkit_module.c` | 扩展生命周期管理 (MINIT/MSTARTUP, RINIT/RSHUTDOWN), PHP API 函数实现 |
| **Compile** | `opkit_compile.c` | 编译引擎：保存/加载 `.phpc` 文件, 文件 I/O, 序列化/反序列化 |
| **Persist** | `opkit_zend_persist.c` | 将数据复制到持久化内存（运行时） |
| **Persist Calc** | `opkit_zend_persist_calc.c` | 计算持久化结构所需的内存大小 |
| **Util** | `opkit_util_funcs.c` | 工具函数：哈希表持久化、校验和计算、脚本加载 |
| **Wrapper** | `opkit_wrapper.h` | 兼容性宏定义，Zend OPcache 结构复用 |

---

## 3. 核心数据结构

### 3.1 持久化脚本结构

```c
// 扩展的持久化脚本结构
typedef struct _opkit_persistent_script {
    zend_persistent_script script;           // Zend 基础脚本结构
    HashTable constants_table;               // 脚本定义的常量表
} opkit_persistent_script;
```

### 3.2 Zend 持久化脚本结构 (来自 OPcache)

```c
typedef struct _zend_persistent_script {
    zend_script    script;                   // 编译后的脚本内容
    zend_long      compiler_halt_offset;     // __HALT_COMPILER 位置
    int            ping_auto_globals_mask;   // 使用的自动全局变量
    accel_time_t   timestamp;                // 脚本修改时间
    bool           corrupted;                // 是否损坏
    bool           is_phar;                  // 是否为 Phar 文件
    uint32_t       num_warnings;             // 编译警告数量
    uint32_t       num_early_bindings;       // 早期绑定数量
    zend_error_info **warnings;              // 编译警告
    zend_early_binding *early_bindings;      // 早期绑定信息
    void          *mem;                      // 共享内存区域
    size_t         size;                     // 使用的内存大小
    struct zend_persistent_script_dynamic_members dynamic_members;
} zend_persistent_script;
```

### 3.3 文件缓存元信息

```c
typedef struct _zend_file_cache_metainfo {
    char         magic[8];                   // 文件魔数 "PHPC\0"
    char         system_id[32];              // 系统 ID (PHP 版本 + 架构)
    size_t       mem_size;                   // 内存大小
    size_t       str_size;                   // 字符串大小
    size_t       script_offset;              // 脚本偏移量
    time_t       timestamp;                  // 时间戳
    uint32_t     checksum;                   // Adler-32 校验和
    size_t       metadata_size;              // 元数据区域大小
    size_t       code_size;                  // 代码区域大小
    size_t       data_size;                  // 数据区域大小
    size_t       misc_size;                  // 杂项区域大小
} zend_file_cache_metainfo;
```

### 3.4 脚本节点（运行时）

```c
typedef struct _opkit_script_node {
    zend_string *filename;                   // 文件名
    char *orig_path;                         // 原始路径
    char *current_path;                      // 当前路径
    zend_persistent_script *script;          // 持久化脚本
    zend_op_array *main_op_array;            // 主操作码数组
    bool executed;                           // 是否已执行 (PHP 8.4+)
    struct _opkit_script_node *next;
    struct _opkit_script_node *prev;
} opkit_script_node;
```

---

## 4. 内存管理架构

### 4.1 影子内存分区

OpKit 使用逻辑内存分区来优化缓存效率：

```
┌────────────────────────────────────────────────────────────────┐
│                     Shadow Memory Layout                        │
├────────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐                                           │
│  │  Metadata Area  │ 脚本头、哈希表、类/函数元数据              │
│  │   (ADD_SIZE_MD) │ 大小: opkit_metadata_size                  │
│  ├─────────────────┤                                           │
│  │    Code Area    │ 操作码 (opcodes)、字面量                   │
│  │   (ADD_SIZE_CD) │ 大小: opkit_code_size                      │
│  ├─────────────────┤                                           │
│  │    Data Area    │ 常量、字符串、属性默认值                   │
│  │   (ADD_SIZE_DT) │ 大小: opkit_data_size                      │
│  ├─────────────────┤                                           │
│  │    Misc Area    │ 对齐、缓冲区、临时数据                     │
│  │   (ADD_SIZE_MS) │ 大小: opkit_misc_size                      │
│  └─────────────────┘                                           │
└────────────────────────────────────────────────────────────────┘
```

### 4.2 内存分区宏定义

```c
#define ADD_SIZE_MD(s) opkit_metadata_size += ZEND_ALIGNED_SIZE(s)  // 元数据
#define ADD_SIZE_CD(s) opkit_code_size += ZEND_ALIGNED_SIZE(s)      // 代码
#define ADD_SIZE_DT(s) opkit_data_size += ZEND_ALIGNED_SIZE(s)      // 数据
#define ADD_SIZE_MS(s) opkit_misc_size += ZEND_ALIGNED_SIZE(s)      // 杂项
```

### 4.3 内存分配流程

```
┌────────────────────────────────────────────────────────────────┐
│                    Memory Allocation Flow                       │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. 计算阶段 (zend_accel_script_persist_calc)                  │
│     ├── 遍历脚本结构                                            │
│     ├── 计算各部分所需内存大小                                  │
│     └── 累加到 metadata/code/data/misc_size                     │
│                          │                                      │
│                          ▼                                      │
│  2. 分配阶段 (emalloc)                                          │
│     ├── 总大小 = MD + CD + DT + MS                              │
│     └── ZCG(mem) = emalloc(total_size)                          │
│                          │                                      │
│                          ▼                                      │
│  3. 持久化阶段 (zend_accel_script_persist)                      │
│     ├── 复制脚本结构到 ZCG(mem)                                 │
│     ├── 递归复制所有子结构                                      │
│     └── 使用 xlat_table 避免重复复制                            │
│                          │                                      │
│                          ▼                                      │
│  4. 文件存储阶段 (opkit_compile_script_store)                   │
│     ├── 写入 metainfo 头                                        │
│     ├── 写入序列化后的脚本                                      │
│     └── 可选：使用 TripleDES 加密                               │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 5. 编译流程详解

### 5.1 编译时流程

```
┌──────────────────────────────────────────────────────────────────┐
│                      Compilation Flow                             │
└──────────────────────────────────────────────────────────────────┘

  PHP Source File (*.php)
           │
           ▼
  ┌─────────────────┐
  │  PHP Compiler   │  zend_compile_file()
  │                 │  - 解析 PHP 代码
  │                 │  - 生成 AST
  │                 │  - 编译为 Opcodes
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Move Symbols   │  opkit_compile_file()
  │                 │  - 移动函数到 script.function_table
  │                 │  - 移动类到 script.class_table
  │                 │  - 移动常量到 constants_table
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │ Calculate Size  │  zend_accel_script_persist_calc()
  │                 │  - 计算各部分内存需求
  │                 │  - 初始化 xlat_table
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │   Persist Data  │  zend_accel_script_persist()
  │                 │  - 复制到连续内存块
  │                 │  - 序列化指针为偏移量
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Serialize      │  opkit_serialize_persistent_script()
  │                 │  - 将指针转换为相对偏移
  │                 │  - 准备存储格式
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Write to File  │  opkit_compile_script_store()
  │                 │  - 写入 metainfo
  │                 │  - 写入脚本数据
  │                 │  - 可选加密
  └────────┬────────┘
           │
           ▼
    Binary File (*.phpc)
```

### 5.2 运行时加载流程

```
┌──────────────────────────────────────────────────────────────────┐
│                      Runtime Loading Flow                         │
└──────────────────────────────────────────────────────────────────┘

  Binary File (*.phpc)
           │
           ▼
  ┌─────────────────┐
  │   Load File     │  opkit_compile_script_load()
  │                 │  - 读取 metainfo
  │                 │  - 验证魔数和系统 ID
  │                 │  - 验证校验和
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Deserialize    │  opkit_deserialize_persistent_script()
  │                 │  - 将偏移量还原为指针
  │                 │  - 重建数据结构
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Fix Pointers   │  递归修复所有内部指针
  │                 │  - fix_script_pointers()
  │                 │  - fix_op_array_pointers()
  │                 │  - fix_class_entry_pointers()
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │  Register       │  opkit_boot() / opkit_load()
  │                 │  - 注册函数到 CG(function_table)
  │                 │  - 注册类到 CG(class_table)
  │                 │  - 注册常量
  │                 │  - 处理早期绑定
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐
  │    Execute      │  zend_execute()
  │                 │  - 执行主 op_array
  └─────────────────┘
```

---

## 6. 序列化与反序列化

### 6.1 指针序列化

将内存指针转换为相对于脚本基地址的偏移量：

```c
#define SERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ptr = (void*)((char*)(ptr) - (char*)script->mem); \
    } \
} while (0)
```

### 6.2 指针反序列化

将偏移量还原为实际内存指针：

```c
#define UNSERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ptr = (void*)((char*)script->mem + (uintptr_t)(ptr)); \
    } \
} while (0)
```

### 6.3 需要处理的指针类型

- `zend_op_array.opcodes` - 操作码数组
- `zend_op_array.literals` - 字面量数组
- `zend_op_array.vars` - 变量名数组
- `zend_op_array.arg_info` - 参数信息
- `zend_op_array.static_variables` - 静态变量
- `zend_class_entry.function_table` - 类方法表
- `zend_class_entry.properties_info` - 属性信息表
- `zend_class_entry.constants_table` - 类常量表
- `zend_class_entry.interfaces` - 接口数组
- `zend_string` - 字符串结构

---

## 7. PHP 版本兼容性

### 7.1 版本检测宏

```c
#if PHP_VERSION_ID >= 80400
    // PHP 8.4+ 代码
#elif PHP_VERSION_ID >= 80300
    // PHP 8.3 代码
#else
    // PHP 8.2 代码
#endif
```

### 7.2 PHP 8.4 主要兼容性修改

| 特性 | PHP 8.2/8.3 | PHP 8.4+ |
|------|-------------|----------|
| `doc_comment` | 存在于 `zend_property_info` | 已移除 |
| Property Hooks | 不支持 | 支持 (4种钩子) |
| `prop_info` | 不存在于 `zend_op_array` | 存在 |
| Runtime Cache | 自动管理 | 需要手动清理 |

### 7.3 Property Hooks 支持

```c
#if PHP_VERSION_ID >= 80400
#define ZEND_PROPERTY_HOOK_COUNT 4  // get, set, isset, unset
#define ZEND_PROPERTY_HOOK_STRUCT_SIZE (sizeof(zend_function*) * ZEND_PROPERTY_HOOK_COUNT)
#endif
```

**序列化注意事项**：
```c
// 正确做法：先保存原始指针，再序列化
zend_function **hooks = prop->hooks;  // 先保存
SERIALIZE_PTR(prop->hooks);            // 这会修改 prop->hooks 为偏移量
for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
    if (hooks[i]) {                    // 使用保存的指针
        SERIALIZE_PTR(hooks[i]);
    }
}
```

**持久化关键**：
```c
if (copy->hooks[i]) {
    zend_op_array *hook = (zend_op_array *)copy->hooks[i];
    hook = zend_shared_memdup_put(hook, sizeof(zend_op_array));
    hook->prop_info = copy;  // 关键：在 persist 前设置
    zend_persist_op_array_ex(hook, ZCG(current_persistent_script));
}
```

---

## 8. CLI 工具 (phpc)

### 8.1 功能模块

```
bin/phpc
├── 参数解析 (getopt)
│   └── 支持 CLI 参数和配置文件 (opkit.json)
│
├── 编译模式 (-s -o)
│   ├── 递归目录扫描
│   ├── 增量编译 (mtime + System ID)
│   └── 调用 opkit_compile_file()
│
├── 入口生成 (-e)
│   └── 调用 opkit_gen_entry_file()
│
├── Phar 打包 (-p)
│   ├── 压缩支持 (gz, bz2)
│   ├── 签名支持 (sha1, sha256, sha512, openssl)
│   └── 自动入口生成
│
├── 静态分析 (-a, analyze)
│   ├── 符号冲突检测
│   └── 内存使用统计
│
└── 信息查看 (-i, info)
    └── 调用 opkit_get_info()
```

### 8.2 入口文件生成

生成的 `entry.php` 示例：
```php
<?php
// OpKit auto-generated entry file
if (!extension_loaded('opkit')) {
    die("Error: OpKit extension not loaded.\n");
}
__DIR__ !== '' && chdir(__DIR__);
$result = opkit_boot();
exit($result ?? 0);
```

### 8.3 配置文件格式 (opkit.json)

```json
{
    "src": "src/",
    "output": "dist/",
    "phar": "app.phar",
    "entry": "dist/entry.php",
    "force": false,
    "no-incremental": false,
    "compress": "gz",
    "sign": "sha256"
}
```

---

## 9. 关键 API 函数

### 9.1 PHP 扩展函数

| 函数 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `opkit_compile_file()` | `$output_dir`, `$source_file` | bool | 编译 PHP 文件为 .phpc |
| `opkit_boot()` | - | mixed | 加载并执行编译后的脚本 |
| `opkit_load()` | `$filename` | bool | 加载 .phpc 文件（不执行） |
| `opkit_get_info()` | `$filename` | array | 获取 .phpc 文件信息 |
| `opkit_gen_entry_file()` | `$filepath` | bool | 生成入口文件 |
| `opkit_reset()` | - | void | 重置已加载脚本 |

### 9.2 内部 C 函数

| 函数 | 文件 | 说明 |
|------|------|------|
| `opkit_compile_file()` | `opkit_compile.c` | 主编译函数 |
| `opkit_compile_script_store()` | `opkit_compile.c` | 存储脚本到文件 |
| `opkit_compile_script_load()` | `opkit_compile.c` | 从文件加载脚本 |
| `zend_accel_script_persist()` | `opkit_zend_persist.c` | 持久化脚本到内存 |
| `zend_accel_script_persist_calc()` | `opkit_zend_persist_calc.c` | 计算内存需求 |
| `zend_accel_load_script()` | `opkit_util_funcs.c` | 加载脚本到运行时 |

---

## 10. 构建系统

### 10.1 构建流程

```bash
# 1. 准备构建环境
phpize

# 2. 配置
./configure --with-php-config=/path/to/php-config --enable-debug

# 3. 编译
make

# 4. 测试
NO_INTERACTION=1 make test TESTS=tests/
```

### 10.2 多版本 PHP 支持

```bash
# PHP 8.2
php-src/php-8.2.30/scripts/phpize && \
./configure --with-php-config=php-src/php-8.2.30/scripts/php-config && \
make

# PHP 8.3
php-src/php-8.3.30/scripts/phpize && \
./configure --with-php-config=php-src/php-8.3.30/scripts/php-config && \
make

# PHP 8.4
php-src/php-8.4.19/scripts/phpize && \
./configure --with-php-config=php-src/php-8.4.19/scripts/php-config && \
make
```

---

## 11. 测试架构

### 11.1 测试文件组织

| 测试文件 | 描述 | 关键特性 |
|----------|------|----------|
| `01_basic.phpt` | 基础编译测试 | 简单函数和输出 |
| `02_directory.phpt` | 目录递归编译 | 多文件编译 |
| `03_relative_path.phpt` | 相对路径测试 | 命名空间类支持 |
| `03_phar_relative_path.phpt` | Phar 相对路径 | Phar + 命名空间 |
| `04_phar.phpt` | 基础 Phar 加载 | Phar 归档 |
| `06_phpc_tool.phpt` | phpc CLI 工具 | 命令行编译 |
| `07_constants.phpt` | 常量测试 | 类和常量 |
| `08_phpc_config.phpt` | 配置文件 | opkit.json |
| `09_phpc_incremental.phpt` | 增量编译 | mtime 检查 |
| `18_property_hooks.phpt` | 属性钩子 | PHP 8.4+ |

### 11.2 测试格式

```php
--TEST--
Test description
--EXTENSIONS--
opkit
--FILE--
<?php
// Test code
?>
--EXPECT--
Expected output
```

---

## 12. 安全与限制

### 12.1 系统 ID 验证

`.phpc` 文件包含系统 ID，确保只在与编译环境兼容的系统上运行：

```c
// 系统 ID 包含：PHP 版本、架构、编译选项
if (!zend_string_equals(system_id, opkit_system_id)) {
    // 系统不匹配，拒绝加载
}
```

### 12.2 OPcache 冲突

**OpKit 与 Zend OPcache 严格不兼容**，必须禁用 OPcache：

```ini
; 正确配置
zend_extension=opkit.so
phar.readonly=Off
;zend_extension=opcache.so  <-- 必须注释掉
```

### 12.3 加载限制

- 必须使用 `zend_extension` 加载，不能使用 `extension`
- 需要 `phar.readonly=Off` 才能创建 Phar 包
- 文件系统权限要求（读取 .phpc 文件）

---

## 13. 性能优化

### 13.1 校验和计算

使用 SSE2 优化的 Adler-32 算法：

```c
#ifdef __SSE2__
// 使用 SIMD 指令加速校验和计算
__m128i read = _mm_loadu_si128((__m128i *) buf);
// ... SIMD 处理
#else
// 标量回退实现
#endif
```

### 13.2 增量编译

通过 mtime 和 System ID 检查避免不必要的重新编译：

```php
if ($incremental && !$force && file_exists($target_file)) {
    if (filemtime($src_file) <= filemtime($target_file)) {
        $info = opkit_get_info($target_file);
        if ($info && $info['system_id_match']) {
            $should_compile = false;  // 跳过编译
        }
    }
}
```

---

## 14. 文件组织

```
opkit/
├── src/                          # C 源代码
│   ├── php_opkit.h              # 主头文件
│   ├── opkit_module.c/h         # 扩展生命周期和 API
│   ├── opkit_compile.c/h        # 编译和文件 I/O
│   ├── opkit_zend_persist.c     # 数据持久化
│   ├── opkit_zend_persist_calc.c # 内存计算
│   ├── opkit_util_funcs.c/h     # 工具函数
│   ├── opkit_wrapper.h          # 兼容性包装器
│   ├── opkit_arginfo.h          # PHP 函数参数信息
│   └── opkit.stub.php           # PHP API 定义
├── bin/                          # CLI 工具
│   └── phpc                     # PHP 编译脚本
├── composer/                     # Composer 插件
│   └── src/Plugin.php
├── tests/                        # 测试文件
│   ├── 01_basic.phpt
│   ├── 02_directory.phpt
│   └── ...
├── config.m4                     # Autotools 配置
└── CLAUDE.md                     # 开发指南
```

---

## 15. 扩展开发指南

### 15.1 添加新 API 函数的步骤

1. **编辑 `src/opkit.stub.php`**：添加 PHP 函数签名
2. **生成 arginfo**：`make` 自动生成 `src/opkit_arginfo.h`
3. **实现函数**：在 `src/opkit_module.c` 中添加 C 实现
4. **添加测试**：在 `tests/` 目录创建 `.phpt` 测试文件

### 15.2 调试技巧

```bash
# GDB 调试
gdb --args php -d zend_extension=./modules/opkit.so test.php

# 检查内存泄漏
php -d memory_limit=256M test.php

# 验证 OPcache 是否禁用
php -m | grep -i opcache  # 应该无输出
```

---

## 16. 参考资料

- [Zend OPcache 源码](https://github.com/php/php-src/tree/master/ext/opcache)
- [PHP Internals Book](https://www.phpinternalsbook.com/)
- [Zend Engine 文档](https://github.com/php/php-src/tree/master/Zend)