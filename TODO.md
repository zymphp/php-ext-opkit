# OpKit 待办事项 (TODOs)

> **优先级说明**：🔴 高优先级（重要功能/修复）｜🟡 中优先级（功能完善）｜🟢 低优先级（研究/优化）

## 核心特性
- [x] **常量支持**: 支持用户定义的持久化常量（包括 `define()` 和类常量）。
- [x] **多版本兼容性**: 支持 PHP 8.2、8.3、8.4、8.5 版本。
- [x] **🟡 PHP 8.5 OPcache 兼容**: 在 PHP 8.5 上实现深度 OPcache 集成——临时恢复原始 `compile_file` 绕过 OPcache 钩子，编译后恢复。
- [ ] **🟢 JIT 优化支持**: 研究 OpKit 与 JIT 的集成方案。JIT 是运行时热点代码编译优化技术，需探索与 Zend OPcache JIT 的共存机制或实现独立的 JIT 编译器。

## 工具与易用性
- [x] **phpc 配置支持**: 支持通过 `opkit.json` 配置文件管理编译参数。
- [x] **🟡 phpc 配置增强**: 支持多路径配置（数组形式）、通配符匹配（如 `src/*/Controller.php`）及 `exclude` 忽略路径配置。
- [x] **增量编译**: 基于源码修改时间、System ID 和 Magic 值的智能增量编译。
- [x] **交互式信息查看**: 增强 `phpc -i` 以支持列出详细的方法签名和类属性。
  - [x] **🟡 显示类属性的完整类型信息**（如 `public string $name`）
- [x] **Composer 支持**: 提供 Composer 插件，支持自动编译及扩展的编译安装。

## 测试与质量
- [x] **静态分析支持**: 增强 `opkit_get_info` 导出详细元数据，实现 `phpc --stubs` 生成 PHP 定义存根，新增 `phpc analyze` 指令用于冲突检测。
- [x] **🔴 完善常量与属性测试**: 增加对普通常量（`define()`/`const`）、类属性（含类型、默认值、访问修饰符）、类常量（含可见性修饰符）的全面测试用例。
  - 新增 `tests/20_constants_comprehensive.phpt` - 测试 namespace const、define()、类常量（含可见性修饰符）、trait 常量、interface 常量
  - 新增 `tests/21_properties_comprehensive.phpt` - 测试类型属性、可见性、静态属性、readonly、联合类型、默认值
  - 新增 `tests/22_constants_properties_integration.phpt` - 测试继承、抽象类、final 类、常量作为属性默认值
- [ ] **🟢 全面基准测试套件**: 开发标准基准测试，用于评估和展示不同类型应用程序的性能提升。

## 性能与稳定性
- [x] **内存池优化**: 实现 Metadata、Code、Data、Misc 四分区布局，减少碎片并提高 CPU 缓存命中率。
- [x] **并发安全性**: 实现原子写入（临时文件 + 重命名）和文件锁，确保多进程环境下文件不会损坏。
- [x] **性能分析**: `phpc` 提供详细的单个文件编译时间和总耗时统计。
- [x] **友好错误提示**: 编译失败时捕获并显示具体错误原因，支持跳过错误文件并继续批量编译。
- [x] **🔴 opkit_boot 返回值**: 当前返回类型为 `mixed`，当入口函数无返回值时会返回 NULL。应修复为只返回 `int`，无返回值或返回非 int 时返回 0。
- [x] **🔴 新增已加载检测函数**: 新增函数如 `opkit_is_loaded(string $filename): bool` 用于检查指定的 `.phpc` 文件是否已被加载，避免重复加载导致冲突。
- [x] **🟡 优化 entry.php 生成格式**: `opkit_gen_entry_file` 生成的入口文件存在多余换行，需优化代码生成逻辑使输出更紧凑。

## 平台支持
- [ ] **🟢 Windows 支持**: 验证并修复 Windows 系统上的兼容性问题（路径处理、文件锁定等）。

## 高级 Phar 支持
- [x] **归档优化**: 支持 Phar 压缩（GZip、BZip2）和数字签名。

## 测试结果汇总 (2026-05-14)

### 已修复 (2026-05-05)

**✅ 嵌套数组常量序列化 (Test 20)**
- 测试文件: `20_constants_comprehensive.phpt`
- 原始问题: 嵌套数组常量 `['key' => [1,2,3]]` 的内部数组在序列化时发生字符串转换 (`Array`)
- 根因: 嵌套数组的持久化路径不完整
- 修复: `zend_persist_zval` 中对 `IS_ARRAY` 类型已实现完整的递归持久化（通过 `zend_hash_persist` 和 `zend_persist_zval` 递归处理数组元素）。测试 20 和 22 在 PHP 8.2/8.3/8.4/8.5 下均通过。

**✅ IS_CONSTANT_AST 内存泄漏 (原 Test 22 崩溃)**
- 测试文件: `22_constants_properties_integration.phpt`
- 原始问题: 运行时崩溃 (Termsig=11) 及 4×32-byte `zend_ast_ref` 内存泄漏
- 根因: PHP 编译器 arena 在 `zend_compile()` 返回前被销毁，但 `IS_CONSTANT_AST` 值（属性/参数默认值中的常量引用）仍指向已释放的 arena 内存。persist 阶段通过 `zend_persist_ast()` 调用 `efree(GC_AST(old_ref))` 释放了子指针而非 `old_ref` 本身，导致泄漏。
- 修复: 在 `opkit_compile_file()` 中，注册完文件级常量后，遍历所有结构（类属性表、静态成员表、类常量、方法/函数 literals）调用 `zval_update_constant_ex()` 将 `IS_CONSTANT_AST` 解析为实际值。这样 persist 阶段不会遇到已释放的 arena 指针。

**✅ phpc 配置增强**
- 问题: 仅支持单个 `src` 路径，不支持通配符和排除
- 修复: `src` 支持字符串或数组，支持 `glob()` 通配符模式（`*`, `?`, `[]`），新增 `exclude` 配置项（`fnmatch` 匹配），配置路径可相对于 opkit.json 所在目录

**✅ opkit_boot 返回值限定为 int**
- 问题: 返回类型为 `mixed`，入口函数无返回值时返回 NULL
- 修复: 两处调用 `main()` 的位置均检查返回值类型，仅 `IS_LONG` 直接返回，其他情况返回 0。同时更新 stub.php 和 arginfo 的返回类型声明为 `int`。

### 已修复 (2026-05-14) —— enum 编译内存泄漏与 doc_comment 泄漏

**✅ Enum case AST 内存泄漏（编译含 enum 的代码库时泄漏）**
- 问题: 编译 `neuron-core`（360 个文件）时报告 603 个内存泄漏，其中 562 个来自 `zend_string.h`，30 个来自 `zend_ast.c`
- 根因 1 (enum): `opkit_update_constant_safe()` 对 `ZEND_AST_CONST_ENUM_INIT` 调用 `opkit_copy_ast_ref()` 复制 AST 后，未释放 PHP 编译器通过 `zend_ast_copy()` 分配的原始堆上 AST，导致每个 enum case 泄漏一个 `zend_ast_ref`（含内部字符串）
- 根因 2 (doc_comment): `src/opkit_zend_persist.c` 中对 `doc_comment` 的持久化被 `#if PHP_VERSION_ID < 80400` 错误包裹。PHP 8.4/8.5 的 `zend_op_array`、`zend_class_entry`、`zend_class_constant` 仍保留 `doc_comment` 字段，导致 doc comment 字符串既未持久化也未释放
- 修复:
  - `src/opkit_compile.c`: 替换 enum AST 前调用 `zend_ast_destroy(ast); efree(Z_AST_P(zv));`
  - `src/opkit_zend_persist.c`: `op_array->doc_comment` 和 `zend_class_constant->doc_comment` 移除错误版本限制；`zend_class_entry->doc_comment` 按版本区分（8.4+ 在 union 外，8.2/8.3 在 `info.user` 内）；`zend_property_info->doc_comment` 保持 `< 80400`（PHP 8.4 确实移除了该字段）
  - `src/opkit_zend_persist.c`: AST 持久化从 `zend_shared_memdup_put_free` 改为 `zend_shared_memdup`，因为 arena 上的 AST 不应被 `efree`；堆分配的 AST ref 在 `IS_CONSTANT_AST` 处理分支中通过 `zval_ptr_dtor_nogc` 释放
  - `src/opkit_wrapper.h`: 补充定义 `zend_shared_memdup` → `_opkit_shared_memdup_put`
- 验证: PHP 8.2-8.5 全版本编译 `neuron-core` 后 0 泄漏，测试全部通过

**✅ Persistence 后堆资源清理（dynamic_func_defs / static_variables）**
- 问题: `zend_persist_op_array_ex` 通过 `_opkit_shared_memdup_put_free_*()` 释放了 opcodes/arg_info 等，但 `literals`、`dynamic_func_defs`、`static_variables` HashTable 结构仍留在堆上，导致大量字符串和对象泄漏
- 根因: `zend_hash_persist` 会释放 HashTable 的 `arData`，导致持久化后无法安全遍历 `function_table`/`class_table` 来定位并清理这些残留资源
- 修复:
  - `src/opkit_compile.c`: 新增 `opkit_op_array_list` 收集器，在 `zend_accel_script_persist()` **之前**收集所有 op_array 指针（main_op_array + file-level functions + class methods + property hooks）
  - 持久化完成后调用 `opkit_destroy_op_array_safe()`：递归释放 `dynamic_func_defs` 数组、释放 `static_variables` HashTable 结构本身（arData 已被 `zend_hash_persist` 释放，不可二次释放）
  - 新增 `opkit_free_ast_ref_list()` 统一释放 `opkit_copy_ast_ref` 创建的堆上 AST ref
  - 成功路径和 `store_failure` 失败路径均执行清理，避免异常退出时泄漏

**✅ phpc CLI 类自动加载**
- 问题: 编译包含跨文件类引用的代码（如 enum case 或 `new` 默认参数）时，`zval_update_constant_ex` 触发 `zend_lookup_class`，若类未加载会导致 fatal error
- 修复: `bin/phpc` 在编译前预扫描所有源文件，构建 FQCN → 文件路径映射，注册 `spl_autoload_register` 回调按需 `require_once`，解决编译时类依赖问题

### 测试结果汇总 (2026-05-14)

| PHP 版本 | 通过 | 跳过 | 失败 | 通过率 |
|---------|------|------|------|--------|
| PHP 8.2.30 | 29 | 5 | 0 | 100% |
| PHP 8.3.30 | 29 | 5 | 0 | 100% |
| PHP 8.4.19 | 30 | 4 | 0 | 100% |
| PHP 8.5.4  | 31 | 3 | 0 | 100% |

### 新增测试文件 (2026-05-14)
- `tests/31_compile_file_basepath.phpt` - `opkit_compile_file` 显式 base_path 保留目录结构
- `tests/32_compile_file_no_basepath.phpt` - `opkit_compile_file` 无 base_path 向后兼容（扁平输出）
- `tests/33_enum_basic.phpt` - Enum 支持（backed/unbacked，默认值）

### 跳过的测试
- `tests/05_triple_des.phpt` - 需要 openssl 扩展
- `tests/18_property_hooks.phpt` - PHP 8.4+ 专属（在 8.2/8.3 跳过）
- `tests/24_php85_fcc_const.phpt` - PHP 8.5+ 专属（在 8.2/8.3/8.4 跳过）
- `tests/27_fork_shm.phpt` / `tests/29_shm_reset_fork.phpt` - 需要 pcntl 扩展

### 已修复 (2026-05-14 #2) —— 编译失败清理与持久化阶段内存安全

**✅ 编译失败路径全局表清理**
- 问题: 批量编译时某个文件编译失败触发 bailout，`CG(function_table)` / `CG(class_table)` / `EG(zend_constants)` 中残留该文件添加的条目，导致后续编译冲突
- 修复: `opkit_compile_file()` 的 `!op_array` 分支中，通过 `zend_hash_del_bucket` 反向遍历删除超出 `orig_*_count` 的残留条目，同时调用 `opkit_free_ast_ref_list()` 释放已积累的 AST ref

**✅ 持久化后 AST ref 列表 use-after-free**
- 问题: `opkit_free_ast_ref_list()` 在持久化成功后访问 `node->ref`，但持久化阶段 `zend_persist_zval()` 已通过 `efree(old_ref)` 释放了该 ref，导致 use-after-free
- 修复: 新增 `opkit_clear_ast_ref_list()` 仅释放追踪节点本身（不触碰 ref），在持久化成功路径和 `store_failure` 路径调用；编译失败路径保持 `opkit_free_ast_ref_list()`（持久化未运行，ref 仍有效）

**✅ 联合类型 arena 检查（`zend_persist_type`）**
- 问题: 联合类型 `A|B`（两个类引用）的类型列表在编译阶段由 arena 分配。`zend_compile()` 返回后 arena 已销毁，持久化时 `_opkit_shared_memdup_put_free_ms` 尝试 `efree()` arena 指针，破坏 ZendMM 堆
- 修复: `zend_persist_type()` 增加 `ZEND_TYPE_USES_ARENA(*type) || zend_accel_in_shm(old_list)` 判断，arena 类型使用 `_opkit_shared_memdup_put_ms`（拷贝后不释放）

**✅ 字符串 Enum FQN >= 41 字符崩溃**
- 问题: 3-case string-backed enum 在 FQN >= 41 字符时 `zend_mm_heap corrupted`（如 `NeuronAI\Chat\Enums\AttachmentContentType` 41字符）
- 根因: `zend_persist_zval_calc` 的 `IS_CONSTANT_AST` 分支仅处理 `ZEND_AST_ZVAL` / `ZEND_AST_CONSTANT`，跳过 `ZEND_AST_CONST_ENUM_INIT`，导致未为 enum case AST（含 `zend_ast_ref` 包装、AST 节点、3 个子节点）预留内存。持久化阶段写入时溢出共享内存块
- 修复: `zend_persist_zval_calc` 增加 else 分支调用 `zend_persist_ast_calc` 处理其他 AST 类型（含 enum init）；`zend_persist_zval` 补充 `GC_SET_REFCOUNT` / `GC_ADD_FLAGS(GC_IMMUTABLE)` / `efree(old_ref)` 与 OPcache 对齐

### 🔴 已知问题 (2026-05-14)

**✅ 编译顺序导致的跨文件类依赖**（已修复，见 2026-05-14 #3）
- ~~问题: `opkit_compile_file` 编译每个文件后将类/函数从全局表中 `zend_accel_move_user_*` 移出，导致后续文件编译时找不到之前的类~~
- ~~影响: neuron-core 编译时 11 个文件报 Class not found~~
- 修复: `bin/phpc` 编译前预扫描所有源文件构建 FQCN→路径映射，注册 `spl_autoload_register`。编译期间遇到未知类时，autoloader 调用 `require_once` 加载依赖文件，类被注册到 `CG(class_table)` 后主编译继续。无需改动 C 代码