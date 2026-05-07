# PHP Zend 编译器选项 (ZEND_COMPILE_*)

本文档汇总 PHP 8.2 到 8.5 所有版本的编译器选项常量。

> **注意**：PHP 8.2 到 8.5 的编译器选项完全相同，以下定义取自 `zend_compile.h`。

---

## 概述

这些常量通过 `CG(compiler_options)` 位掩码组合使用，用于改变 Zend 编译器的默认行为。

```c
/* 设置编译器选项 */
CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;
CG(compiler_options) |= ZEND_COMPILE_DELAYED_BINDING;

/* 检查编译器选项 */
if (CG(compiler_options) & ZEND_COMPILE_WITHOUT_EXECUTION) {
    // ...
}
```

---

## 编译器选项列表

| 位位置 | 常量名 | 十六进制值 | 说明 |
|--------|--------|-----------|------|
| 0 | `ZEND_COMPILE_EXTENDED_STMT` | `0x00001` | 生成扩展语句调试信息 |
| 1 | `ZEND_COMPILE_EXTENDED_FCALL` | `0x00002` | 生成扩展函数调用调试信息 |
| 2 | `ZEND_COMPILE_HANDLE_OP_ARRAY` | `0x00004` | 调用扩展的 op_array 处理器 |
| 3 | `ZEND_COMPILE_IGNORE_INTERNAL_FUNCTIONS` | `0x00008` | 对内部函数使用 `ZEND_INIT_FCALL_BY_NAME` 代替 `ZEND_INIT_FCALL` |
| 4 | `ZEND_COMPILE_IGNORE_INTERNAL_CLASSES` | `0x00010` | 不处理继承自内部类的早期绑定 |
| 5 | `ZEND_COMPILE_DELAYED_BINDING` | `0x00020` | 生成 `ZEND_DECLARE_CLASS_DELAYED` 操作码延迟早期绑定 |
| 6 | `ZEND_COMPILE_NO_CONSTANT_SUBSTITUTION` | `0x00040` | 禁用编译时常量替换 |
| 7 | *(保留)* | `0x00080` | 位 7 未使用 |
| 8 | `ZEND_COMPILE_NO_PERSISTENT_CONSTANT_SUBSTITUTION` | `0x00100` | 禁用持久常量的编译时替换 |
| 9 | `ZEND_COMPILE_IGNORE_USER_FUNCTIONS` | `0x00200` | 对用户函数使用 `ZEND_INIT_FCALL_BY_NAME` |
| 10 | `ZEND_COMPILE_GUARDS` | `0x00400` | 强制所有类使用 `ZEND_ACC_USE_GUARDS` |
| 11 | `ZEND_COMPILE_NO_BUILTINS` | `0x00800` | 禁用内置函数的特殊处理 |
| 12 | `ZEND_COMPILE_WITH_FILE_CACHE` | `0x01000` | 编译结果可存储到文件缓存 |
| 13 | `ZEND_COMPILE_IGNORE_OTHER_FILES` | `0x02000` | 忽略在其他文件中声明的函数和类 |
| 14 | `ZEND_COMPILE_WITHOUT_EXECUTION` | `0x04000` | `opcache_compile_file()` 调用时设置，只编译不执行 |
| 15 | `ZEND_COMPILE_PRELOAD` | `0x08000` | preloading 期间调用时设置 |
| 16 | `ZEND_COMPILE_NO_JUMPTABLES` | `0x10000` | 禁用 switch 语句的跳转表优化 |
| 17 | `ZEND_COMPILE_PRELOAD_IN_CHILD` | `0x20000` | 在独立进程的 preloading 中调用时设置 |
| 18 | `ZEND_COMPILE_IGNORE_OBSERVER` | `0x40000` | 忽略 observer 通知，用于编译后手动处理 |

---

## 详细说明

### 调试相关

#### `ZEND_COMPILE_EXTENDED_STMT` (1<<0)
生成扩展的语句调试信息，用于代码覆盖率分析等场景。

#### `ZEND_COMPILE_EXTENDED_FCALL` (1<<1)
生成扩展的函数调用调试信息。

#### `ZEND_COMPILE_EXTENDED_INFO`
上述两者的组合：
```c
#define ZEND_COMPILE_EXTENDED_INFO (ZEND_COMPILE_EXTENDED_STMT|ZEND_COMPILE_EXTENDED_FCALL)
```

---

### 扩展处理

#### `ZEND_COMPILE_HANDLE_OP_ARRAY` (1<<2)
启用 op_array 处理器回调，允许 Zend 扩展在编译完成后处理 op_array。

---

### 函数处理

#### `ZEND_COMPILE_IGNORE_INTERNAL_FUNCTIONS` (1<<3)
对内部函数生成 `ZEND_INIT_FCALL_BY_NAME` 而不是 `ZEND_INIT_FCALL`，避免运行时函数解析优化。

#### `ZEND_COMPILE_IGNORE_USER_FUNCTIONS` (1<<9)
对用户定义的函数生成 `ZEND_INIT_FCALL_BY_NAME`。

#### `ZEND_COMPILE_NO_BUILTINS` (1<<11)
禁用对 `strlen()`、`array_merge()` 等内置函数的特殊优化处理。

---

### 类处理

#### `ZEND_COMPILE_IGNORE_INTERNAL_CLASSES` (1<<4)
不处理继承自内部类的早期绑定（early binding）。在命名空间中假设编译时不存在的内部类可能在运行时出现。

#### `ZEND_COMPILE_DELAYED_BINDING` (1<<5)
生成 `ZEND_DECLARE_CLASS_DELAYED` 操作码，将类的早期绑定延迟到运行时执行。用于处理类继承依赖关系。

#### `ZEND_COMPILE_GUARDS` (1<<10)
强制所有类使用属性访问守卫（`ZEND_ACC_USE_GUARDS`），用于实现魔术方法的拦截。

---

### 常量处理

#### `ZEND_COMPILE_NO_CONSTANT_SUBSTITUTION` (1<<6)
完全禁用编译时的常量替换，所有常量引用都保留为运行时查找。

#### `ZEND_COMPILE_NO_PERSISTENT_CONSTANT_SUBSTITUTION` (1<<8)
只禁用持久常量（define 定义的常量）的编译时替换。

---

### 缓存相关

#### `ZEND_COMPILE_WITH_FILE_CACHE` (1<<12)
标记编译结果可能存储到文件缓存（OPcache 文件缓存），影响某些优化决策。

---

### 跨文件处理

#### `ZEND_COMPILE_IGNORE_OTHER_FILES` (1<<13)
忽略在其他文件中声明的函数和类。用于 `opcache_compile_file()` 只收集当前文件定义的符号。

---

### 预加载相关

#### `ZEND_COMPILE_WITHOUT_EXECUTION` (1<<14)
当编译器由 `opcache_compile_file()` 调用时设置。表示只编译脚本而不执行。

**OpKit 使用此标志：**
```c
CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;
```

#### `ZEND_COMPILE_PRELOAD` (1<<15)
当编译器在 preloading 期间被调用时设置。

#### `ZEND_COMPILE_PRELOAD_IN_CHILD` (1<<17)
当编译器在 preloading 的独立子进程中被调用时设置。

---

### 优化控制

#### `ZEND_COMPILE_NO_JUMPTABLES` (1<<16)
禁用 switch 语句的跳转表（jumptable）优化，强制使用线性比较。

---

### Observer 扩展

#### `ZEND_COMPILE_IGNORE_OBSERVER` (1<<18)
忽略 observer 扩展的通知，允许在编译完成后的后处理步骤中手动触发通知。

---

## 默认值

```c
/* 默认编译器选项 */
#define ZEND_COMPILE_DEFAULT ZEND_COMPILE_HANDLE_OP_ARRAY

/* eval() 中的默认选项（无任何选项） */
#define ZEND_COMPILE_DEFAULT_FOR_EVAL 0
```

---

## OpKit 使用的编译器选项

OpKit 在编译 PHP 脚本时设置以下选项：

```c
CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;      // 只编译不执行
CG(compiler_options) |= ZEND_COMPILE_IGNORE_INTERNAL_CLASSES; // 不处理内部类继承
CG(compiler_options) |= ZEND_COMPILE_DELAYED_BINDING;         // 延迟类绑定
CG(compiler_options) |= ZEND_COMPILE_HANDLE_OP_ARRAY;         // 启用 op_array 处理器
CG(compiler_options) |= ZEND_COMPILE_IGNORE_OBSERVER;         // 忽略 observer 通知
CG(compiler_options) |= ZEND_COMPILE_WITH_FILE_CACHE;         // 标记可存储到文件缓存
CG(compiler_options) |= ZEND_COMPILE_IGNORE_OTHER_FILES;      // 忽略其他文件的符号
```

### 各选项用途

| 选项 | OpKit 用途 |
|------|-----------|
| `ZEND_COMPILE_WITHOUT_EXECUTION` | 标记这是 opcache_compile_file() 调用，编译的脚本不会被执行 |
| `ZEND_COMPILE_IGNORE_INTERNAL_CLASSES` | 避免编译时因内部类不存在而出错，假设运行时内部类可用 |
| `ZEND_COMPILE_DELAYED_BINDING` | 将类继承的早期绑定延迟到运行时，处理复杂的类依赖关系 |
| `ZEND_COMPILE_HANDLE_OP_ARRAY` | 启用 op_array 处理器回调，允许 Zend 扩展在编译完成后处理 op_array |
| `ZEND_COMPILE_IGNORE_OBSERVER` | 忽略 observer 扩展的通知，编译后手动处理 |
| `ZEND_COMPILE_WITH_FILE_CACHE` | 标记编译结果可能存储到文件缓存，影响优化决策 |
| `ZEND_COMPILE_IGNORE_OTHER_FILES` | 只收集当前编译文件定义的函数/类/常量，忽略 include 的其他文件 |
