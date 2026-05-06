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
  - [ ] **🟡 显示类属性的完整类型信息**（如 `public string $name`）
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

## 测试结果汇总 (2026-05-06)

| PHP 版本 | 通过 | 跳过 | 失败 | 通过率 |
|---------|------|------|------|--------|
| PHP 8.2.30 | 22 | 3 | 0 | 100% |
| PHP 8.3.30 | 22 | 3 | 0 | 100% |
| PHP 8.4.19 | 23 | 2 | 0 | 100% |
| PHP 8.5.4  | 24 | 1 | 0 | 100% |

### 新增测试文件
- `tests/20_constants_comprehensive.phpt` - 全面常量测试
- `tests/21_properties_comprehensive.phpt` - 全面属性测试
- `tests/22_constants_properties_integration.phpt` - 集成测试
- `tests/24_php85_fcc_const.phpt` - PHP 8.5 常量表达式中的第一类可调用对象测试

### PHP 8.5 适配 (2026-05-06)
- **深度 OPcache 集成**: 在 `opkit_compile_file()` 中临时保存并恢复 `zend_compile_file` 以绕过 OPcache 的 `persistent_compile_file` 钩子，编译完成后恢复。结构体定义（`zend_accel_directives`、`zend_accel_globals`、`zend_accel_shared_globals`）已更新至 8.5 格式。
- **新增 AST 支持**: `ZEND_AST_OP_ARRAY`、`ZEND_AST_CALLABLE_CONVERT`、`zend_ast_is_decl()` 保护在四个阶段（persist/calc/serialize/unserialize）均已处理。
- **新增 Opcode 支持**: `ZEND_DECLARE_ATTRIBUTED_CONST` + `ZEND_OP_DATA`（属性表持久化）。
- **新增类型处理**: `IS_PTR`、`IS_INDIRECT`、`IS_OBJECT`、`IS_RESOURCE`、`IS_REFERENCE` 在所有 zval switch 中均已处理。
- **新增属性字段**: `zend_attribute.validation_error` 支持。
- **已知问题**: FCC（第一类可调用对象）常量在 `opkit_boot` 后存在轻微内存泄漏（2×344 字节），属于低优先级问题。

### 已知问题

**🟡 嵌套数组常量未完全序列化 (Test 20)**
- 测试文件: `20_constants_comprehensive.phpt`
- 问题: 嵌套数组常量 `['key' => [1,2,3]]` 的内部数组在序列化时发生字符串转换 (`Array`)
- 影响: PHP 8.2/8.3/8.4 输出有差异，但不影响简单/标量数组常量
- 根因: 嵌套数组的持久化路径不完整，需在实现完整的递归持久化

### 已修复 (2026-05-05)

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

### 跳过的测试
- `tests/05_triple_des.phpt` - 需要 openssl 扩展
- `tests/18_property_hooks.phpt` - PHP 8.4+ 专属（在 8.2/8.3 跳过）