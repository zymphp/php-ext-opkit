# OpKit 执行模型行为报告

## 摘要

OpKit 支持在多种 PHP 执行模型下运行：单进程常驻内存、多进程常驻内存（通过共享内存 fork 共享）以及传统的 PHP-FPM 多 Worker 模型。不同模型下，`opkit_load()` 的内存分配行为、`opkit_boot()` 的符号注册持久性、`RSHUTDOWN` 的清理策略以及共享内存（SHM）的实际效果存在显著差异。本报告基于源代码分析，详细阐述各模型下的完整行为链路、关键限制和正确使用方式。

---

## 1. 核心生命周期机制

### 1.1 三阶段模型

OpKit 的运行时由三个核心阶段构成：

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  opkit_load │ ──► │ opkit_boot  │ ──► │ RSHUTDOWN   │
│  (加载阶段)  │     │  (引导阶段)  │     │ (清理阶段)   │
└─────────────┘     └─────────────┘     └─────────────┘
```

#### 加载阶段 (`opkit_load` / `opkit_load_multi`)

1. 读取 `.phpc` 文件头（验证 `magic`、`system_id`、`checksum`）
2. 根据 `opkit.shm_size` 配置选择内存分配路径：
   - **SHM 路径**：若共享内存有足够空间，调用 `opkit_shared_alloc()` 从 `mmap(MAP_SHARED|MAP_ANONYMOUS)` 映射的共享内存中分配
   - **堆路径**：否则回退到 `emalloc()` 分配进程私有堆内存
3. 将文件数据读入内存缓冲区
4. `zend_file_cache_unserialize()` 将偏移量还原为指针，重建 `zend_persistent_script` 结构
5. `opkit_keep_memory()` 创建 `opkit_script_node`，将脚本指针和内存块指针存入进程私有的 `loaded_scripts` 链表

> **关键行为**：`opkit_script_node->in_shm` 字段标记内存来源。若来自 SHM，则设为 `true`。

#### 引导阶段 (`opkit_boot`)

1. 遍历 `loaded_scripts` 链表，将每个脚本的函数、类、常量注册到 Zend Engine 全局表：
   - `EG(function_table)` — 函数注册
   - `EG(class_table)` — 类注册（含 `opkit_link_classes()` 解析继承链）
   - `EG(zend_constants)` — 常量注册
2. 执行每个脚本的 `main_op_array`（处理顶层 `define()`、`const` 等副作用）
3. 标记 `node->executed = true`
4. 调用入口函数（默认 `main`）

> **关键行为**：符号注册是每个进程**独立进行**的。即使脚本数据存储在 SHM 中，`opkit_boot()` 仍需在每个进程内将类/函数指针写入进程私有的 Zend 全局表。

#### 清理阶段 (`RSHUTDOWN`)

`PHP_RSHUTDOWN_FUNCTION(opkit)` 调用 `opkit_reset_script()`，执行以下操作：

1. **符号注销**：`opkit_clean_script_items()` 从 `EG(function_table)`、`EG(class_table)`、`EG(zend_constants)` 中删除当前进程已注册的符号
2. **Runtime Cache 释放**：若 `node->executed == true` 且 `fn_flags & ZEND_ACC_HEAP_RT_CACHE`，释放堆分配的 runtime cache
3. **内存释放**：
   - 若 `node->in_shm == false`：调用 `efree(node->mem_to_free)` 释放堆内存
   - 若 `node->in_shm == true`：**不释放** SHM 内存（`mmap` 区域由系统统一管理）
4. **链表清理**：释放所有 `opkit_script_node` 节点，将 `loaded_scripts` 置为 `NULL`

> **关键行为**：RSHUTDOWN 后，`loaded_scripts` 链表为空，`opkit_boot()` 再次调用将抛出 `"No script loaded for opkit_boot"` 异常。

---

## 2. 模型一：单进程常驻内存

**适用场景**：CLI 脚本、Swoole/RoadRunner/ReactPHP 等常驻内存服务器、自定义事件循环。

### 2.1 无 SHM 模式（默认，`opkit.shm_size = 0`）

```
进程启动
    │
    ▼
┌──────────────────┐
│ opkit_load()     │ ──► emalloc() 分配堆内存
│                  │ ──► loaded_scripts 链表新增节点
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ opkit_boot()     │ ──► 注册符号到 Zend Engine
│                  │ ──► 执行 main_op_array + 入口函数
└────────┬─────────┘
         │
         ▼
    处理请求 1 ──► 符号已注册，直接调用
    处理请求 2 ──► 符号已注册，直接调用
         │
         ▼
┌──────────────────┐
│ RSHUTDOWN        │ ──► 注销符号
│ (进程退出时)      │ ──► efree() 释放堆内存
│                  │ ──► 清空 loaded_scripts
└──────────────────┘
```

**行为特征**：
- `opkit_load()` 和 `opkit_boot()` 通常在 **worker 启动阶段**（如 Swoole `onWorkerStart`）执行一次
- 符号在进程生命周期内**常驻**，多个请求共享同一套注册表
- 堆内存持续占用，直到进程退出才释放
- 每个进程独立持有完整的脚本数据副本（内存不共享）

**限制**：
- 不适用于标准 PHP-FPM（每个请求后 RSHUTDOWN 会清理）
- 多进程部署时内存不共享，每个进程一份副本

### 2.2 SHM 模式（`opkit.shm_size > 0`）

```
进程启动
    │
    ▼
MINIT ──► mmap(MAP_SHARED|MAP_ANONYMOUS) 创建共享内存段
    │
    ▼
┌──────────────────┐
│ opkit_load()     │ ──► opkit_shared_alloc() 从 SHM 分配
│                  │ ──► 数据写入 SHM
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ opkit_boot()     │ ──► 符号注册到进程私有 Zend 表
└────────┬─────────┘
         │
         ▼
    处理请求 ──► 符号常驻
         │
         ▼
RSHUTDOWN ──► 注销符号，但 SHM 数据保留
              loaded_scripts 链表清空
```

**行为特征**：
- 脚本数据存储在 SHM 中，但符号注册仍在进程私有表中
- RSHUTDOWN 只清理符号和 bookkeeping，不释放 SHM 物理页
- 由于 `loaded_scripts` 被清空，**下次需要重新 `opkit_load()` 才能再次 `opkit_boot()`**

> **重要**：在单进程常驻内存模型中，如果框架在每次请求后触发 RSHUTDOWN（标准 PHP SAPI 行为），则 `loaded_scripts` 会被清空。但 Swoole/RoadRunner 等框架通常在 worker 生命周期内不触发 RSHUTDOWN，因此符号和链表得以保留。

---

## 3. 模型二：多进程常驻内存（Fork + SHM 共享）

**适用场景**：父进程预加载脚本后 `fork()` 多个子进程（如自定义进程管理器、预加载 Worker Pool）。

### 3.1 完整行为链路

```
父进程 (Preloader)
    │
    ▼
MINIT ──► mmap(MAP_SHARED|MAP_ANONYMOUS) 创建 SHM
    │
    ▼
┌──────────────────┐
│ opkit_load()     │ ──► 从 SHM 分配空间，写入 .phpc 数据
│ opkit_boot()     │ ──► 在父进程中注册符号（可选）
└────────┬─────────┘
         │
         ▼
    fork() ───────────────────────┬──────────────────────┐
         │                        │                      │
         ▼                        ▼                      ▼
   子进程 A                   子进程 B               子进程 C
   (继承 SHM 映射)            (继承 SHM 映射)         (继承 SHM 映射)
         │                        │                      │
         ▼                        ▼                      ▼
   ┌─────────────┐         ┌─────────────┐        ┌─────────────┐
   │ 无需重新加载 │         │ 无需重新加载 │        │ 无需重新加载 │
   │ 直接 boot() │         │ 直接 boot() │        │ 直接 boot() │
   │ (注册符号)  │         │ (注册符号)  │        │ (注册符号)  │
   └─────────────┘         └─────────────┘        └─────────────┘
```

### 3.2 关键行为分析

**Fork 继承机制**：
- `mmap(MAP_SHARED|MAP_ANONYMOUS)` 创建的内存映射在 `fork()` 时由子进程完整继承
- 子进程与父进程共享**相同的物理内存页**，实现零拷贝数据共享
- 但 `loaded_scripts` 链表是**进程私有**的（存储在堆内存中），子进程不继承父进程的链表

**子进程的正确使用方式**：

由于子进程不继承 `loaded_scripts` 链表，子进程有两种使用方式：

**方式 A：父进程加载但不 boot，子进程独立 boot**
```php
// 父进程
opkit_load('app.phpc');  // 数据写入 SHM
// 不调用 opkit_boot()

fork();

// 子进程
// loaded_scripts 为空！无法直接 opkit_boot()
// 需要重新建立 loaded_scripts 与 SHM 数据的关联
```

> **问题**：当前 OpKit 没有提供让子进程"重新关联"已存在于 SHM 中的脚本数据的 API。子进程的 `loaded_scripts` 为空，`opkit_boot()` 会失败。

**方式 B：父进程加载并 boot，子进程再次 load + boot**
```php
// 父进程
opkit_load('app.phpc');
opkit_boot();

fork();

// 子进程
opkit_load('app.phpc');  // 再次从 SHM 分配空间（新的 offset）
opkit_boot();
```

> **问题**：每次 `opkit_load()` 都会从 SHM bump allocator 中分配新空间。`opkit_shared_alloc` 的 `pos` 只增不减。多次 fork 后的子进程各自 `load()` 会导致 SHM 空间重复占用，直到耗尽。

**方式 C：每个子进程独立 load（当前最可行的方式）**
```php
// 子进程启动时
opkit_load('app.phpc');  // 从 SHM 分配
opkit_boot();
// 处理请求...
// RSHUTDOWN 清理符号，保留 SHM 数据
// 但 loaded_scripts 被清空，下次请求需重新 load（消耗新的 SHM 空间）
```

### 3.3 SHM 分配器的行为限制

`opkit_shared_alloc` 是一个简单的 bump allocator：

```c
void *opkit_shared_alloc(size_t size) {
    void *retval = (char *)opkit_shm_segment->p + opkit_shm_segment->pos;
    opkit_shm_segment->pos += block_size;
    return retval;
}
```

- **分配**：`pos` 向前移动，永不自动回退
- **释放**：RSHUTDOWN 时若 `in_shm == true`，**不回收**已分配空间
- **重置**：仅 `opkit_shm_reset()` 可将 `pos` 重置为 0（带进程间锁）

**在多进程模型下的后果**：
- 每个子进程每次 `opkit_load()` 都消耗新的 SHM 空间
- 无自动垃圾回收机制
- 需要显式调用 `opkit_shm_reset()` 才能回收，但这会影响所有共享该 SHM 的进程

---

## 4. 模型三：PHP-FPM

**适用场景**：传统 PHP-FPM 多 Worker 架构，每个 Worker 处理多个请求。

### 4.1 PHP-FPM 生命周期与 OpKit 交互

```
Master 进程启动
    │
    ▼
加载 php.ini ──► 所有扩展 MINIT
    │              └── OpKit MINIT: mmap() 创建 SHM (若 shm_size > 0)
    ▼
    fork() ───────────────────────────────────────────┐
         │                                              │
         ▼                                              ▼
    Worker 1                                        Worker 2
    (继承 SHM 映射)                                  (继承 SHM 映射)
         │                                              │
    ┌────┴──────────────────────────┐             ┌────┴──────────────────────────┐
    │ Request 1                      │             │ Request 1                      │
    │   ├── RINIT                    │             │   ├── RINIT                    │
    │   ├── execute PHP script       │             │   ├── execute PHP script       │
    │   │   ├── opkit_load()         │             │   │   ├── opkit_load()         │
    │   │   │   └── SHM/堆分配       │             │   │   │   └── SHM/堆分配       │
    │   │   ├── opkit_boot()         │             │   │   ├── opkit_boot()         │
    │   │   │   └── 注册符号         │             │   │   │   └── 注册符号         │
    │   │   └── 业务逻辑             │             │   │   └── 业务逻辑             │
    │   └── RSHUTDOWN                │             │   └── RSHUTDOWN                │
    │       └── opkit_reset_script() │             │       └── opkit_reset_script() │
    │           ├── 注销符号         │             │           ├── 注销符号         │
    │           ├── efree(堆内存)    │             │           ├── efree(堆内存)    │
    │           └── 保留 SHM         │             │           └── 保留 SHM         │
    │                                  │             │                                  │
    │ Request 2                      │             │ Request 2                      │
    │   ├── opkit_load() ← 必须     │             │   ├── opkit_load() ← 必须     │
    │   ├── opkit_boot()            │             │   ├── opkit_boot()            │
    │   └── ...                      │             │   └── ...                      │
    └──────────────────────────────────┘             └──────────────────────────────────┘
```

### 4.2 关键行为分析

**每次请求的完整链路**：

1. **RINIT**：OpKit 无特殊操作（无 RINIT hook）
2. **Execute**：PHP 脚本执行
   - 若脚本调用 `opkit_load()`：从文件读取 .phpc，分配内存（SHM 或堆），反序列化，加入 `loaded_scripts`
   - 若脚本调用 `opkit_boot()`：遍历 `loaded_scripts`，注册符号，执行 `main_op_array`，调用入口函数
3. **RSHUTDOWN**：`opkit_reset_script()` 被自动调用
   - 注销所有符号（函数/类/常量从 Zend 全局表中移除）
   - 释放堆内存（若有）
   - 保留 SHM 内存（不回收）
   - **清空 `loaded_scripts` 链表**

**对下次请求的影响**：
- `loaded_scripts == NULL`，所以 `opkit_boot()` 会抛出 `"No script loaded for opkit_boot"`
- 必须重新执行 `opkit_load()` 才能再次使用

### 4.3 SHM 在 PHP-FPM 下的实际效果

| 场景 | 行为 | 结果 |
|------|------|------|
| 首次请求 `opkit_load()` | SHM 有足够空间，从 SHM 分配 | 数据存入 SHM，物理页共享 |
| 请求结束 RSHUTDOWN | 符号注销，保留 SHM 数据 | SHM `pos` 不重置 |
| 下次请求 `opkit_load()` | SHM 再次分配（新的 offset） | **重复占用 SHM 空间** |
| N 次请求后 | SHM 空间耗尽 | 回退到 `emalloc()` 堆分配 |

**结论**：在 PHP-FPM 的标准使用模式下（每次请求都重新 load + boot），启用 SHM 的**优势被严重削弱**：
- SHM 中的旧数据不会被复用（因为 `loaded_scripts` 被清空）
- SHM 空间被重复占用，最终耗尽后回退到堆分配
- 文件 I/O 仍然发生（每次请求都读取 .phpc 文件）

### 4.4 PHP-FPM 下的正确用法

在 PHP-FPM 中，OpKit 的核心价值不在于"常驻内存共享"，而在于**跳过编译阶段**：

```php
<?php
// 每次请求执行

// 替代 include/require：从二进制直接加载已编译的 opcodes
opkit_load(__DIR__ . '/app.phpc');
opkit_boot();

// 现在可以调用 app.phpc 中定义的函数和类
$result = main();
```

与 `include 'app.php'` 相比：
- `include`：读取源码 → 词法分析 → 语法分析 → AST 生成 → 编译为 opcodes → 执行
- `opkit_load + boot`：读取二进制 → 反序列化（指针修复）→ 注册符号 → 执行

**配置建议**：
- PHP-FPM 下**不需要**设置 `opkit.shm_size`（设为 0）
- 使用堆内存分配即可，因为每次请求后内存会被释放
- 开启 SHM 反而会导致内存泄漏（SHM 空间不回收）

---

## 5. 行为对比矩阵

| 维度 | 单进程常驻（无 SHM） | 单进程常驻（SHM） | 多进程 Fork + SHM | PHP-FPM |
|------|---------------------|------------------|------------------|---------|
| **内存位置** | 堆（`emalloc`） | SHM（`mmap`） | SHM（`mmap`） | 堆或 SHM |
| **进程间共享** | 否 | 否（仅同进程） | 是（物理页共享） | 否（Worker 独立） |
| **符号持久性** | 直到 RSHUTDOWN | 直到 RSHUTDOWN | 直到 RSHUTDOWN | 每次请求后清理 |
| **`loaded_scripts`** | 常驻链表 | 常驻链表 | 不继承（fork 后为空） | 每次请求后清空 |
| **多次 load** | 堆内存累积 | SHM 空间累积 | SHM 空间累积 | 堆/SHM 累积 |
| **是否需要每次重新 load** | 否 | 否 | 是（链表不继承） | 是（RSHUTDOWN 清空） |
| **SHM 回收** | N/A | 仅 `opkit_shm_reset()` | 仅 `opkit_shm_reset()` | 仅 `opkit_shm_reset()` |
| **推荐配置** | `shm_size=0` | `shm_size > 0`（可选） | `shm_size > 0` | `shm_size=0` |
| **核心价值** | 跳过编译 | 跳过编译 | 父进程预加载，子进程共享数据 | 跳过编译 |

---

## 6. 关键限制与注意事项

### 6.1 SHM 不是垃圾回收的

`opkit_shared_alloc` 是一个 bump allocator，已分配的空间**永远不会自动回收**。在以下场景中会导致空间耗尽：

- PHP-FPM 每次请求都 `opkit_load()`
- 多进程模型中每个子进程独立 `opkit_load()`
- 单进程模型中反复加载不同脚本

**解决方案**：在适当的时机调用 `opkit_shm_reset()`（会清理当前进程符号并重置 SHM allocator）。注意这会同时影响所有共享该 SHM 的进程。

### 6.2 符号注册是进程私有的

即使脚本数据存储在 SHM 中，`opkit_boot()` 执行的符号注册操作（写入 `EG(function_table)` 等）仍然是**进程私有的**。这意味着：

- 每个进程/Worker 都需要独立调用 `opkit_boot()`
- `fork()` 后的子进程不会继承父进程的符号注册表
- SHM 节省的是"脚本数据内存"，而非"符号注册开销"

### 6.3 RSHUTDOWN 清理是彻底的

`opkit_reset_script()` 在 RSHUTDOWN 中会：
- 从 Zend 全局表中**删除**所有已注册符号（函数/类/常量）
- 释放 `loaded_scripts` 链表
- 释放堆内存

这意味着在标准 PHP SAPI（如 PHP-FPM）中，OpKit 的状态**不会跨请求保留**。

### 6.4 当前缺乏"子进程关联 SHM 数据"的 API

`fork()` 后的子进程继承了 SHM 映射，但 `loaded_scripts` 链表被清空。当前没有 API 允许子进程直接"挂载"父进程已加载到 SHM 中的脚本数据而不重新分配空间。这是多进程 SHM 共享模型的主要限制。

---

## 7. 使用建议

### 7.1 PHP-FPM 用户

```ini
; php.ini
opkit.shm_size = 0  ; 不需要共享内存
```

```php
<?php
// 每次请求的入口脚本
opkit_load(__DIR__ . '/compiled/app.phpc');
exit(opkit_boot());
```

**期望效果**：每次请求从二进制加载，跳过 PHP 编译阶段，比 `include` 更快。

### 7.2 常驻内存服务器用户（Swoole / RoadRunner）

```ini
; php.ini
opkit.shm_size = 0  ; 或设为适当值（若需要多 Worker 共享）
```

```php
<?php
// Worker 启动时执行一次（如 Swoole onWorkerStart）
$scripts = glob(__DIR__ . '/compiled/*.phpc');
opkit_load_multi($scripts);
opkit_boot();

// 此后多个请求直接复用已注册的符号
// 注意：确保框架不在每次请求后触发 RSHUTDOWN
```

### 7.3 多进程预加载用户

```php
<?php
// 父进程预加载
opkit_shm_reset();  // 确保干净的 SHM
opkit_load('app.phpc');
// 当前子进程需要重新 load 才能 boot，这是设计限制
// 考虑在子进程启动时也执行 load + boot
```

---

## 8. 附录：源码关键路径速查

| 功能 | 函数/宏 | 文件 |
|------|---------|------|
| SHM 分配器初始化 | `opkit_shared_alloc_startup()` | `src/opkit_shared_alloc.c` |
| SHM 分配 | `opkit_shared_alloc()` | `src/opkit_shared_alloc.c` |
| SHM 重置 | `opkit_shm_reset()` | `src/opkit_shared_alloc.c` |
| 判断是否 SHM | `opkit_accel_in_shm()` | `src/opkit_shared_alloc.c` |
| 加载 .phpc | `opkit_compile_script_load()` | `src/opkit_compile.c` |
| 保留脚本引用 | `opkit_keep_memory()` | `src/opkit_module.c` |
| 符号注册/链接 | `opkit_boot()` | `src/opkit_module.c` |
| 符号注销 | `opkit_clean_script_items()` | `src/opkit_module.c` |
| 请求清理 | `opkit_reset_script()` | `src/opkit_module.c` |
| RSHUTDOWN Hook | `PHP_RSHUTDOWN_FUNCTION(opkit)` | `src/opkit_module.c` |
