# OpKit .phpc 文件格式规范

## 概述

`.phpc` (PHP Compiled) 是 OpKit 扩展使用的二进制预编译文件格式。它将 PHP 脚本的编译结果（Opcode、常量、类、函数等）持久化为二进制文件，供后续快速加载执行，无需重新解析和编译源码。

---

## 文件结构

```
┌─────────────────────────────────────────────────────────────┐
│              File Header (112 bytes on 64-bit)              │
├─────────────────────────────────────────────────────────────┤
│  Offset  │  Size   │  Field                                    │
├──────────┼─────────┼───────────────────────────────────────────┤
│    0     │   8     │  magic[8]       - 文件标识 "PHPC\0\0\0\0" │
│    8     │   32    │  system_id[32]  - PHP 系统标识           │
│   40     │   8     │  mem_size       - 内存数据区大小         │
│   48     │   8     │  str_size       - 字符串池大小           │
│   56     │   8     │  script_offset  - 脚本结构偏移量         │
│   64     │   8     │  timestamp      - 文件编译时间戳         │
│   72     │   4     │  checksum       - Adler32 校验和         │
│   76     │   4     │  (padding)      - 对齐填充               │
│   80     │   8     │  metadata_size  - Metadata 分区大小      │
│   88     │   8     │  code_size      - Code 分区大小          │
│   96     │   8     │  data_size      - Data 分区大小          │
│  104     │   8     │  misc_size      - Misc 分区大小          │
└─────────────────────────────────────────────────────────────┘
```

---

## Header 详细说明

### Magic (8 bytes)
```c
char magic[8] = "PHPC\0\0\0\0";
```
文件标识符，用于快速识别 .phpc 文件格式。

### System ID (32 bytes)
```c
char system_id[32];
```
PHP 系统的唯一标识，基于 PHP 版本、编译选项、扩展等生成。用于确保 .phpc 文件与当前 PHP 环境兼容，防止不兼容的二进制文件被加载。

### Memory Sizes (8 bytes each)
```c
size_t mem_size;      // 主内存数据区大小
size_t str_size;      // 字符串池大小
```
- `mem_size`: 序列化后的 `zend_persistent_script` 结构及其关联数据的总大小
- `str_size`: 所有 interned 字符串的序列化后总大小

### Script Offset (8 bytes)
```c
size_t script_offset;
```
`zend_persistent_script` 结构在内存数据区中的偏移量。通常为 0，表示脚本结构从数据区起始位置开始。

### Timestamp (8 bytes)
```c
time_t timestamp;
```
文件编译时的时间戳，用于增量编译时比较源文件修改时间。

### Checksum (4 bytes)
```c
uint32_t checksum;
```
使用 Adler32 算法计算内存数据区和字符串池的校验和，用于验证文件完整性。

```c
checksum = zend_adler32(ADLER32_INIT, mem_data, mem_size);
checksum = zend_adler32(checksum, string_pool, str_size);
```

### Partition Sizes (8 bytes each)
```c
size_t metadata_size;  // Metadata 分区大小
size_t code_size;      // Code 分区大小
size_t data_size;      // Data 分区大小
size_t misc_size;      // Misc 分区大小
```
用于统计和调试，显示各逻辑分区的内存占用情况。

---

## 内存数据区结构

内存数据区存储序列化后的 `zend_persistent_script` 结构，包含以下主要部分：

### 1. zend_persistent_script 结构
```c
typedef struct _zend_persistent_script {
    zend_script    script;              // 脚本基本信息
    zend_long      compiler_halt_offset;// __HALT_COMPILER 位置
    int            ping_auto_globals_mask;
    accel_time_t   timestamp;           // 编译时间戳
    bool           corrupted;           // 是否损坏
    bool           is_phar;             // 是否为 phar 包
    bool           empty;               // 是否为空
    uint32_t       num_warnings;        // 警告数量
    uint32_t       num_early_bindings;  // 早期绑定数量
    zend_error_info **warnings;         // 编译警告
    zend_early_binding *early_bindings; // 早期绑定信息
    void          *mem;                 // 共享内存指针
    size_t         size;                // 内存大小
    // ... 动态成员
} zend_persistent_script;
```

### 2. zend_script 子结构
```c
typedef struct _zend_script {
    zend_string   *filename;            // 原始文件名
    HashTable      class_table;         // 类定义表
    HashTable      function_table;      // 函数定义表
    zend_op_array  main_op_array;       // 主 op_array（顶层代码）
} zend_script;
```

### 3. OpKit 扩展结构 (opkit_persistent_script)
```c
typedef struct _opkit_persistent_script {
    zend_persistent_script script;
    HashTable constants_table;          // 用户定义常量表
} opkit_persistent_script;
```

---

## 序列化机制

### 指针序列化
所有内存指针在序列化时被转换为相对于 `script->mem` 起始地址的偏移量：

```c
#define SERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ptr = (void*)((char*)ptr - (char*)script->mem); \
    } \
} while (0)
```

反序列化时恢复为绝对地址：

```c
#define UNSERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ptr = (void*)((char*)buf + (size_t)ptr); \
    } \
} while (0)
```

### 字符串序列化
字符串分为两类处理：

1. **普通字符串**：转换为相对于 `script->mem` 的偏移量
2. **Interned 字符串**：存储在独立的字符串池中，偏移量低 1 位标记为 1

```c
#define IS_SERIALIZED_INTERNED(ptr) ((size_t)(ptr) & Z_UL(1))

// Interned 字符串偏移格式
void *serialized = (void*)(pool_offset | Z_UL(1));
```

### 环形引用处理
使用 xlat 表（转换表）跟踪已序列化的对象，避免重复序列化：

```c
// 序列化前检查
if (zend_shared_alloc_get_xlat_entry(source)) {
    return;  // 已序列化，跳过
}
zend_shared_alloc_register_xlat_entry(source, offset);
```

---

## 数据分区详情

### Metadata Area
存储脚本元数据和结构信息：
- `zend_persistent_script` 结构本身
- `zend_script` 结构
- 类表、函数表的哈希表结构
- 运行时缓存 (`op_array->cache_size`)

### Code Area
存储编译后的 PHP 字节码：
- `zend_op` 指令数组 (`op_array->opcodes`)
- 每个指令包含操作码、操作数、类型等信息

### Data Area
存储运行时数据：
- 字面量 (`op_array->literals`) - zval 数组
- 变量名 (`op_array->vars`)
- 参数信息 (`zend_arg_info`)
- 字符串值

### Misc Area
存储辅助数据结构：
- AST 节点（用于常量表达式）
- 属性 (attributes)
- Try-catch 块 (`zend_try_catch_element`)
- Live ranges
- 静态变量表

---

## 序列化流程

```
编译 PHP 源码
    ↓
创建 zend_persistent_script
    ↓
计算内存需求 (zend_accel_script_persist_calc)
    - 遍历所有结构计算 size
    - 分别累加到四个分区
    ↓
分配内存缓冲区
    ↓
序列化到缓冲区 (zend_file_cache_serialize)
    - 序列化 script 结构
    - 序列化 class_table
    - 序列化 function_table
    - 序列化 main_op_array
    - 序列化常量表
    - 序列化警告和早期绑定
    ↓
写入文件
    - 写入 Header (96 bytes)
    - 写入内存数据区
    - 写入字符串池
    ↓
计算并写入 checksum
```

---

## 反序列化流程

```
读取文件
    ↓
验证 Header
    - 检查 magic
    - 检查 system_id 匹配
    - 验证 checksum
    ↓
分配内存并读取数据
    ├── 堆内存路径: mem = emalloc(total_size)
    └── SHM 路径:   mem = opkit_shared_alloc(total_size)
                      (当 opkit.shm_size > 0 且空间充足时)
    ↓
反序列化 (zend_file_cache_unserialize)
    - 恢复所有指针偏移量
    - 重建哈希表
    - 恢复 interned 字符串
    - 初始化运行时缓存指针
    ↓
类链接 (opkit_link_classes)
    - 解析继承关系
    - 链接父类、接口
    ↓
解析类常量
    - 计算常量表达式
    ↓
执行全局代码 (define, const 等)
```

### 共享内存加载说明

当 `opkit.shm_size` 大于 0 时，反序列化后的脚本数据会存储在 `mmap(MAP_SHARED)` 映射的共享内存中。这意味着：

- **文件数据**仍需要从磁盘读取到临时缓冲区
- **持久化阶段**将数据从临时缓冲区复制到共享内存（通过 `zend_accel_script_persist`）
- **子进程继承**：`fork()` 后子进程继承父进程的共享内存映射，无需重新读取文件
- **内存标记**：`opkit_script_node->in_shm` 标记内存来源，RSHUTDOWN 时 SHM 内存不释放

---

## 增量编译兼容性

增量编译通过以下字段判断是否重新编译：

1. **System ID**: PHP 环境发生变化时，system_id 会改变
2. **Magic**: 文件格式版本变更时会更新
3. **Timestamp**: 与源文件修改时间比较

```c
if (system_id != current_system_id ||
    magic != FILE_CACHE_MAGIC ||
    file_mtime > phpc_mtime) {
    // 需要重新编译
}
```

---

## 安全考虑

1. **System ID 验证**: 防止在不同 PHP 版本或配置间混用 .phpc 文件
2. **Checksum 验证**: 检测文件损坏或篡改
3. **大小限制**: 通过 `max_file_size` 限制单个文件大小
4. **路径隔离**: Phar 模式下的路径修复，防止路径泄露

---

## 调试信息

使用 `phpc -i <file>.phpc` 可以查看文件的元信息和分区统计：

```
File: example.phpc
Magic: PHPC
System ID: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Memory Size: 12345 bytes
String Pool: 6789 bytes
Checksum: 0xAABBCCDD

Partition Sizes:
  Metadata: 4096 bytes
  Code: 4096 bytes
  Data: 2048 bytes
  Misc: 2105 bytes

Classes: 5
Functions: 12
Constants: 3
```