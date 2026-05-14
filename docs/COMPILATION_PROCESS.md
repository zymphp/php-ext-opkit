# OpKit PHP 脚本编译详细流程

## 目录
1. [概述](#1-概述)
2. [编译流程总览](#2-编译流程总览)
3. [详细编译阶段](#3-详细编译阶段)
4. [序列化机制](#4-序列化机制)
5. [文件格式](#5-文件格式)
6. [运行时加载流程](#6-运行时加载流程)
7. [关键数据结构转换](#7-关键数据结构转换)
8. [PHP 8.4 特殊处理](#8-php-84-特殊处理)
9. [调试和诊断](#9-调试和诊断)
10. [总结](#10-总结)

---

## 1. 概述

OpKit 的编译流程将 PHP 源代码转换为二进制的 `.phpc` 文件，包含完整的 Opcode 和元数据。整个过程分为**编译时**和**运行时**两个阶段。

---

## 2. 编译流程总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         OpKit 编译流程                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  PHP Source (*.php)                                                      │
│       │                                                                  │
│       ▼                                                                  │
│  ┌──────────────────┐                                                    │
│  │ 1. PHP 编译       │  zend_compile_file()                              │
│  │    - 词法分析      │  - 生成 AST                                       │
│  │    - 语法分析      │  - 编译为 Opcodes                                 │
│  │    - 生成 Opcodes │                                                   │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 2. 符号收集       │  opkit_compile_file()                             │
│  │    - 收集函数      │  - 移动到 script->function_table                  │
│  │    - 收集类        │  - 移动到 script->class_table                     │
│  │    - 收集常量      │  - 移动到 constants_table                         │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 3. 内存计算       │  zend_accel_script_persist_calc()                 │
│  │    - 计算各区域大小│  - Metadata/Code/Data/Misc                        │
│  │    - 建立映射表    │  - xlat_table 避免重复计算                        │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 4. 内存分配       │  emalloc(total_size)                              │
│  │    - 分配连续内存  │  - ZCG(mem) 指向内存起始                          │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 5. 数据持久化     │  zend_accel_script_persist()                      │
│  │    - 复制结构到内存│  - 使用 _opkit_shared_memdup_put*                 │
│  │    - 处理指针      │  - 递归处理所有子结构                             │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 6. 序列化         │  zend_file_cache_serialize*                       │
│  │    - 指针转偏移量  │  - SERIALIZE_PTR/SERIALIZE_STR                    │
│  │    - 字符串池化    │  - 字符串存储到独立区域                             │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  ┌──────────────────┐                                                    │
│  │ 7. 文件写入       │  opkit_compile_script_store()                     │
│  │    - 写入文件头    │  - metainfo (magic, system_id, checksum)          │
│  │    - 写入数据      │  - 脚本数据 + 字符串池                             │
│  └────────┬─────────┘                                                    │
│           │                                                              │
│           ▼                                                              │
│  Binary File (*.phpc)                                                    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 详细编译阶段

### 3.1 PHP 编译阶段

```c
// opkit_compile.c: opkit_do_compile_file()
static bool opkit_do_compile_file(zend_string *output_path, zend_string *script_name, zend_string *base_path) {
    zend_file_handle file_handle;
    zend_op_array *op_array = NULL;
    uint32_t orig_compiler_options;
    zend_persistent_script *persistent_script;

    // 初始化文件句柄
    zend_stream_init_filename_ex(&file_handle, script_name);

    // 设置编译选项
    orig_compiler_options = CG(compiler_options);
    CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;  // 只编译不执行
    CG(compiler_options) &= ~ZEND_COMPILE_PRELOAD;

    // 编译文件
    persistent_script = opkit_compile_file(&file_handle, ZEND_REQUIRE, &op_array);

    // 存储编译结果
    if (persistent_script) {
        opkit_compile_script_store(output_path, persistent_script, base_path);
        success = true;
    }

    CG(compiler_options) = orig_compiler_options;
    zend_destroy_file_handle(&file_handle);
    return success;
}
```

### 3.2 符号收集阶段

```c
// opkit_compile.c: opkit_compile_file()
zend_persistent_script *opkit_compile_file(zend_file_handle *file_handle, int type, zend_op_array **op_array_p) {
    zend_persistent_script *new_persistent_script;
    uint32_t orig_functions_count, orig_class_count, orig_constants_count;
    zend_op_array *orig_active_op_array = CG(active_op_array);
    zend_op_array *op_array;
    uint32_t orig_compiler_options = 0;

    // 记录编译前全局表中的数量
    orig_functions_count = CG(function_table)->nNumUsed;
    orig_class_count = CG(class_table)->nNumUsed;
    orig_constants_count = EG(zend_constants)->nNumUsed;

    // 设置编译选项
    CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;
    CG(compiler_options) |= ZEND_COMPILE_IGNORE_INTERNAL_CLASSES;
    CG(compiler_options) |= ZEND_COMPILE_DELAYED_BINDING;
    CG(compiler_options) |= ZEND_COMPILE_HANDLE_OP_ARRAY;
    CG(compiler_options) |= ZEND_COMPILE_IGNORE_OBSERVER;
    CG(compiler_options) |= ZEND_COMPILE_WITH_FILE_CACHE;
    CG(compiler_options) |= ZEND_COMPILE_IGNORE_OTHER_FILES;

    // 临时绕过 OPcache 钩子（如已加载），使用原始编译器
    zend_op_array *(*saved_compile_file)(zend_file_handle*, int) = zend_compile_file;
    extern zend_op_array *compile_file(zend_file_handle*, int);
    zend_compile_file = compile_file;

    // 调用 PHP 编译器
    op_array = zend_compile_file(file_handle, type);
    zend_compile_file = saved_compile_file;  // 恢复 OPcache 钩子

    if (op_array) {
        // 手动注册 ZEND_DECLARE_CONST 定义的常量到全局常量表
        // （因为 ZEND_COMPILE_WITHOUT_EXECUTION 会阻止运行时执行）
        // ...

        // 预解析 IS_CONSTANT_AST：将常量引用解析为实际值
        // 避免 persist 阶段访问已释放的 arena 内存
        // 对 enum case (ZEND_AST_CONST_ENUM_INIT) 不直接解析，而是复制到堆上
        // 防止 zval_update_constant_ex 在类未完全链接时调用 zend_enum_new() 导致 SIGSEGV
        opkit_update_constant_safe(...)  // 对属性默认值、类常量、字面量等

        // 创建持久化脚本结构
        new_persistent_script = create_persistent_script();
        new_persistent_script->script.main_op_array = *op_array;

        // 移动新定义的函数/类/常量到脚本表
        zend_accel_move_user_functions(CG(function_table), ...);
        zend_accel_move_user_classes(CG(class_table), ...);
        zend_accel_move_user_constants(EG(zend_constants), ...);

        // 处理早期绑定
        zend_accel_build_delayed_early_binding_list(new_persistent_script);
        new_persistent_script->warnings = zend_persist_warnings(...);

        efree(op_array);
    }

    CG(compiler_options) = orig_compiler_options;
    return new_persistent_script;
}
```

### 3.3 内存计算阶段

```c
// opkit_zend_persist_calc.c: zend_accel_script_persist_calc()
uint32_t zend_accel_script_persist_calc(zend_persistent_script *script, int for_shm) {
    Bucket *p;
    opkit_persistent_script *op_script = (opkit_persistent_script *)script;

    // 初始化脚本状态
    script->mem = NULL;
    script->size = 0;
    script->corrupted = false;
    ZCG(current_persistent_script) = script;

    // 初始化分区大小计数器
    opkit_metadata_size = 0;
    opkit_code_size = 0;
    opkit_data_size = 0;
    opkit_misc_size = 0;

    // 计算脚本结构本身的大小
    ADD_SIZE_MD(sizeof(opkit_persistent_script));
    ADD_INTERNED_STRING(script->script.filename);

    // 计算类表大小
    if (script->script.class_table.nNumUsed != script->script.class_table.nNumOfElements) {
        zend_hash_rehash(&script->script.class_table);
    }
    zend_accel_persist_class_table_calc(&script->script.class_table);

    // 计算函数表大小
    if (script->script.function_table.nNumUsed != script->script.function_table.nNumOfElements) {
        zend_hash_rehash(&script->script.function_table);
    }
    zend_hash_persist_calc(&script->script.function_table);
    ZEND_HASH_MAP_FOREACH_BUCKET(&script->script.function_table, p) {
        ZEND_ASSERT(p->key != NULL);
        ADD_INTERNED_STRING(p->key);
        zend_persist_op_array_calc(&p->val);
    } ZEND_HASH_FOREACH_END();

    // 计算主 op_array 大小
    zend_persist_op_array_calc_ex(&script->script.main_op_array);

    // 计算常量表大小
    if (op_script->constants_table.nNumUsed != op_script->constants_table.nNumOfElements) {
        zend_hash_rehash(&op_script->constants_table);
    }
    zend_accel_persist_constant_table_calc(&op_script->constants_table);

    // 计算警告和早期绑定大小
    zend_persist_warnings_calc(script->num_warnings, script->warnings);
    zend_persist_early_bindings_calc(script->num_early_bindings, script->early_bindings);

    ZCG(current_persistent_script) = NULL;
    return script->size;
}
```

**内存分区计算详解：**

```
┌─────────────────────────────────────────────────────────────────┐
│                    内存分区计算示例                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Metadata 区域 (ADD_SIZE_MD)                                    │
│  ├── opkit_persistent_script 结构                              │
│  ├── 哈希表头 (HashTable)                                       │
│  ├── 运行时缓存 (op_array->cache_size)                         │
│  └── 属性信息、方法表等元数据                                     │
│                                                                  │
│  Code 区域 (ADD_SIZE_CD)                                        │
│  ├── 操作码数组 (zend_op * op_array->last)                     │
│  └── 跳转目标地址 (jmp_addr)                                    │
│                                                                  │
│  Data 区域 (ADD_SIZE_DT)                                        │
│  ├── 字面量数组 (zval * literals)                               │
│  ├── 默认属性值表                                               │
│  ├── 参数字符串 (zend_string *)                                 │
│  └── 类型信息 (zend_type)                                       │
│                                                                  │
│  Misc 区域 (ADD_SIZE_MS)                                        │
│  ├── AST 节点                                                   │
│  ├── live_range 信息                                            │
│  ├── try_catch_array                                            │
│  └── 对齐填充                                                   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.4 数据持久化阶段

```c
// opkit_zend_persist.c: zend_accel_script_persist()
zend_persistent_script *zend_accel_script_persist(zend_persistent_script *script, int for_shm) {
    Bucket *p;
    zend_persistent_script *new_script;
    opkit_persistent_script *op_script = (opkit_persistent_script *)script;

    // 设置内存基址
    script->mem = ZCG(mem);
    old_script_ptr = op_script;

    // 复制脚本结构到持久化内存
    new_script = zend_shared_memdup_put(old_script_ptr, sizeof(opkit_persistent_script));
    opkit_persistent_script *new_op_script = (opkit_persistent_script *)new_script;

    ZCG(current_persistent_script) = new_script;

    // 持久化文件名
    zend_accel_store_interned_string(new_script->script.filename);

    // 持久化类表
    zend_accel_persist_class_table(&new_script->script.class_table);

    // 持久化函数表
    zend_hash_persist(&new_script->script.function_table);
    ZEND_HASH_MAP_FOREACH_BUCKET(&new_script->script.function_table, p) {
        ZEND_ASSERT(p->key != NULL);
        zend_accel_store_interned_string(p->key);
        zend_persist_op_array(&p->val);
    } ZEND_HASH_FOREACH_END();

    // 持久化主 op_array
    zend_shared_alloc_register_xlat_entry(&old_script_ptr->script.script.main_op_array,
                                          &new_script->script.main_op_array);
    zend_persist_op_array_ex(&new_script->script.main_op_array, new_script);

    // 持久化常量表
    zend_accel_persist_constant_table(&new_op_script->constants_table);

    // 持久化警告和早期绑定
    new_script->warnings = zend_persist_warnings(new_script->num_warnings, new_script->warnings);
    new_script->early_bindings = zend_persist_early_bindings(
        new_script->num_early_bindings, new_script->early_bindings);

    // 计算最终大小
    new_script->size = (char*)ZCG(mem) - (char*)new_script->mem;

    ZCG(current_persistent_script) = NULL;
    return new_script;
}

// 持久化后资源清理
// zend_persist_op_array_ex 通过 _opkit_shared_memdup_put_free_*() 释放了 opcodes/arg_info
// 等，但 dynamic_func_defs 数组和 static_variables HashTable 结构仍留在堆上。
// 由于 zend_hash_persist 会释放 HashTable 的 arData，持久化后无法安全遍历 function_table
// 来定位这些资源。因此我们在持久化前收集所有 op_array 指针，持久化后统一清理。
static void opkit_destroy_op_array_safe(zend_op_array *op_array) {
    if (!op_array) return;

    // 递归释放 dynamic_func_defs 数组本身（内部 op_array 的 opcodes 等已被 persist 释放）
    if (op_array->num_dynamic_func_defs && op_array->dynamic_func_defs) {
        for (uint32_t i = 0; i < op_array->num_dynamic_func_defs; i++) {
            opkit_destroy_op_array_safe(op_array->dynamic_func_defs[i]);
        }
        efree(op_array->dynamic_func_defs);
        op_array->dynamic_func_defs = NULL;
    }

    // 释放 static_variables HashTable 结构（arData 已被 zend_hash_persist 释放）
    if (op_array->static_variables) {
        efree(op_array->static_variables);
        op_array->static_variables = NULL;
    }
}
```

**内存复制宏详解：**

```c
// 通用内存复制宏（仅复制，不释放原内存）
#define _opkit_shared_memdup_put(ptr, size) ({ \
    void *new_p = ZCG(mem); \
    ZCG(mem) = (void*)((char*)ZCG(mem) + ZEND_ALIGNED_SIZE(size)); \
    memcpy(new_p, ptr, size); \
    new_p; \
})

// 带释放的内存复制（仅用于堆分配且不再需要的结构）
static zend_always_inline void *_opkit_shared_memdup_put_free(void *source, size_t size) {
    void *old_p;
    if ((old_p = zend_hash_index_find_ptr(&ZCG(xlat_table), (uintptr_t)source)) != NULL) {
        return old_p;  // 已存在，返回已有副本
    }
    void *new_p = _opkit_shared_memdup_put(source, size);
    efree(source);  // 释放原内存
    return new_p;
}

// xlat_table 用于避免重复复制
static zend_always_inline void _opkit_shared_alloc_register_xlat_entry(const void *key, const void *value) {
    zend_hash_index_update_ptr(&ZCG(xlat_table), (uintptr_t)key, (void*)value);
}

// AST 持久化注意事项：
// 编译器 arena 上的 AST 节点（如 zend_ast_zval、zend_ast_list）不应使用 put_free，
// 因为 arena 内存由 Zend 统一释放。应使用 zend_shared_memdup_put（即 _opkit_shared_memdup_put）。
// 堆分配的 AST ref（如 opkit_copy_ast_ref 创建的）需要在持久化后手动释放。
```

---

## 4. 序列化机制

### 4.1 指针序列化

将内存指针转换为相对于脚本基地址的偏移量：

```c
// 关键宏定义
#define SERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ZEND_ASSERT((char*)(ptr) >= (char*)script->mem && \
                    (char*)(ptr) < (char*)script->mem + script->size); \
        ptr = (void*)((char*)(ptr) - (char*)script->mem); \
    } \
} while (0)

#define UNSERIALIZE_PTR(ptr) do { \
    if (ptr) { \
        ptr = (void*)((char*)script->mem + (uintptr_t)(ptr)); \
    } \
} while (0)
```

### 4.2 字符串序列化

字符串有两种存储方式：普通字符串和驻留字符串（interned）

```c
static void *zend_file_cache_serialize_interned(zend_string *str, zend_file_cache_metainfo *info)
{
    size_t len;
    void *ret;

    // 检查是否已序列化
    ret = zend_shared_alloc_get_xlat_entry(str);
    if (ret) {
        return ret;
    }

    // 计算字符串在字符串池中的位置（标记为 interned：最低位为 1）
    len = _ZSTR_STRUCT_SIZE(ZSTR_LEN(str));
    ret = (void*)(info->str_size | Z_UL(1));  // 标记为 interned
    zend_shared_alloc_register_xlat_entry(str, ret);

    // 确保字符串池有足够空间
    if (info->str_size + len > ZSTR_LEN(current_string_pool)) {
        // 扩展字符串池
        current_string_pool = zend_string_realloc(
            current_string_pool,
            ((_ZSTR_HEADER_SIZE + 1 + new_len + 4095) & ~0xfff) - (_ZSTR_HEADER_SIZE + 1),
            0);
        string_pool_base = (void*)ZSTR_VAL(current_string_pool);
    }

    // 复制字符串到字符串池
    zend_string *new_str = (zend_string *)(ZSTR_VAL(current_string_pool) + info->str_size);
    memcpy(new_str, str, len);
    info->str_size += ZEND_ALIGNED_SIZE(len);
    return ret;
}

// 字符串反序列化
static void *zend_file_cache_unserialize_interned(zend_string *str)
{
    // 从字符串池中恢复
    zend_string *res = (zend_string*)((char*)string_pool_base + ((size_t)(str) & ~Z_UL(1)));
    return zend_new_interned_string(zend_string_copy(res));
}
```

### 4.3 完整序列化流程

```c
// 主序列化函数
static void zend_file_cache_serialize(zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
    // 序列化文件名
    SERIALIZE_STR(script->script.filename);

    // 序列化类表
    zend_file_cache_serialize_hash(&script->script.class_table, script, info, buf,
                                   zend_file_cache_serialize_class);

    // 序列化函数表
    zend_file_cache_serialize_hash(&script->script.function_table, script, info, buf,
                                   zend_file_cache_serialize_func);

    // 序列化主 op_array
    zend_file_cache_serialize_op_array(&script->script.main_op_array, script, info, buf);

    // 序列化常量表
    zend_file_cache_serialize_hash(&((opkit_persistent_script*)script)->constants_table,
                                   script, info, buf, zend_file_cache_serialize_constant);

    // 序列化警告
    zend_file_cache_serialize_warnings(script, info, buf);

    // 序列化早期绑定
    zend_file_cache_serialize_early_bindings(script, info, buf);
}
```

### 4.4 Op_array 序列化细节

```c
static void zend_file_cache_serialize_op_array(zend_op_array *op_array,
                                                zend_persistent_script *script,
                                                zend_file_cache_metainfo *info,
                                                void *buf)
{
    // 1. 序列化静态变量
    if (op_array->static_variables) {
        SERIALIZE_PTR(op_array->static_variables);
        HashTable *ht = op_array->static_variables;
        ht = UNSERIALIZED_PTR(ht);
        zend_file_cache_serialize_hash(ht, script, info, buf, zend_file_cache_serialize_zval);
    }

    // 2. 序列化字面量
    if (op_array->literals) {
        zval *p, *end;
        SERIALIZE_PTR(op_array->literals);
        p = op_array->literals;
        p = UNSERIALIZED_PTR(p);
        if (p) {
            end = p + op_array->last_literal;
            while (p < end) {
                zend_file_cache_serialize_zval(p, script, info, buf);
                p++;
            }
        }
    }

    // 3. 序列化操作码
    SERIALIZE_PTR(op_array->opcodes);
    if (op_array->opcodes) {
        zend_op *opline = op_array->opcodes;
        opline = UNSERIALIZED_PTR(opline);
        if (opline) {
            zend_op *end = opline + op_array->last;
            while (opline < end) {
#if ZEND_USE_ABS_CONST_ADDR
                // 序列化常量操作数指针
                if (opline->op1_type == IS_CONST) {
                    SERIALIZE_PTR(opline->op1.zv);
                }
                if (opline->op2_type == IS_CONST) {
                    SERIALIZE_PTR(opline->op2.zv);
                }
#endif
#if ZEND_USE_ABS_JMP_ADDR
                // 序列化跳转地址
                switch (opline->opcode) {
                    case ZEND_JMP:
                    case ZEND_FAST_CALL:
                        SERIALIZE_PTR(opline->op1.jmp_addr);
                        break;
                    case ZEND_JMPZ:
                    case ZEND_JMPNZ:
                    // ... 其他跳转指令
                        SERIALIZE_PTR(opline->op2.jmp_addr);
                        break;
                }
#endif
                // 序列化操作码处理器
                zend_serialize_opcode_handler(opline);
                opline++;
            }
        }
    }

    // 4. 序列化参数信息
    if (op_array->arg_info) {
        SERIALIZE_PTR(op_array->arg_info);
        // ... 序列化每个参数的名称和类型
    }

    // 5. 序列化函数字符串
    SERIALIZE_STR(op_array->function_name);
    SERIALIZE_STR(op_array->filename);
    SERIALIZE_STR(op_array->doc_comment);  // 所有 PHP 版本均保留

    // 6. 序列化变量名
    if (op_array->vars) {
        SERIALIZE_PTR(op_array->vars);
        // ... 序列化每个变量名
    }

    // 7. 序列化其他指针
    SERIALIZE_PTR(op_array->live_range);
    SERIALIZE_PTR(op_array->scope);
    SERIALIZE_PTR(op_array->try_catch_array);
    SERIALIZE_ATTRIBUTES(op_array->attributes);
}
```

---

## 5. 文件格式

### 5.1 文件结构

```
┌─────────────────────────────────────────────────────────────────┐
│                     .phpc 文件结构                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                    文件头 (metainfo)                     │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │ magic[8]          │ "PHPC\0\0\0\0"              │    │    │
│  │  │ system_id[32]     │ PHP 系统 ID                  │    │    │
│  │  │ mem_size          │ 脚本数据大小                 │    │    │
│  │  │ str_size          │ 字符串池大小                 │    │    │
│  │  │ script_offset     │ 脚本数据偏移                 │    │    │
│  │  │ timestamp         │ 编译时间戳                   │    │    │
│  │  │ checksum          │ Adler-32 校验和              │    │    │
│  │  │ metadata_size     │ 元数据区域大小               │    │    │
│  │  │ code_size         │ 代码区域大小                 │    │    │
│  │  │ data_size         │ 数据区域大小                 │    │    │
│  │  │ misc_size         │ 杂项区域大小                 │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                   脚本数据区域                           │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │ opkit_persistent_script 结构                    │    │    │
│  │  │ ├── script (zend_script)                        │    │    │
│  │  │ │   ├── main_op_array                           │    │    │
│  │  │ │   ├── class_table                             │    │    │
│  │  │ │   └── function_table                          │    │    │
│  │  │ └── constants_table                             │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │ 类定义 (zend_class_entry 数组)                  │    │    │
│  │  │ ├── function_table                              │    │    │
│  │  │ ├── properties_info                             │    │    │
│  │  │ ├── constants_table                             │    │    │
│  │  │ └── default_properties_table                    │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │ 函数定义 (zend_op_array 数组)                   │    │    │
│  │  │ ├── opcodes                                     │    │    │
│  │  │ ├── literals                                    │    │    │
│  │  │ ├── arg_info                                    │    │    │
│  │  │ └── vars                                        │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                   字符串池区域                           │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │ zend_string 1                                   │    │    │
│  │  │ zend_string 2                                   │    │    │
│  │  │ ...                                             │    │    │
│  │  │ zend_string N                                   │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 文件写入代码

```c
// opkit_compile.c: opkit_compile_script_store()
int opkit_compile_script_store(zend_string *output_path, zend_persistent_script *script, zend_string *base_path) {
    zend_file_cache_metainfo info;
    zend_string *full_path;
    void *buf;
    char *script_path;
    int fd;
    ssize_t written;

    // 初始化字符串池
    current_string_pool = zend_string_alloc(4096, 0);
    string_pool_base = (void*)ZSTR_VAL(current_string_pool);

    // 初始化 xlat_table
    zend_shared_alloc_init_xlat_table();

    // 计算内存需求
    zend_accel_script_persist_calc(script, 0);

    // 分配序列化内存
    size_t total_size = script->size + ZSTR_LEN(current_string_pool);
    buf = emalloc(total_size);

    // 持久化脚本到内存
    zend_accel_script_persist(script, 0);

    // 序列化脚本
    memset(&info, 0, sizeof(info));
    memcpy(info.magic, "PHPC", 5);  // Magic: 5字节 "PHPC"（包括结尾的\0）
    memcpy(info.system_id, opkit_system_id, 32);
    info.mem_size = script->size;
    info.str_size = ZSTR_LEN(current_string_pool);  // 字符串池实际使用大小
    info.script_offset = 0;
    info.timestamp = time(NULL);
    info.metadata_size = opkit_metadata_size;
    info.code_size = opkit_code_size;
    info.data_size = opkit_data_size;
    info.misc_size = opkit_misc_size;

    // 执行序列化
    zend_file_cache_serialize(script, &info, buf);

    // 计算校验和
    info.checksum = zend_adler32(0, (unsigned char*)buf, info.mem_size);
    info.checksum = zend_adler32(info.checksum, (unsigned char*)string_pool_base, info.str_size);

    // 写入文件
    full_path = opkit_compile_get_phpc_file_path(ZSTR_VAL(output_path), base_path, script->script.filename);
    fd = opkit_compile_open(ZSTR_VAL(full_path), O_CREAT|O_RDWR|O_TRUNC|O_BINARY, 0644);

    // 写入元信息头
    written = write(fd, &info, sizeof(info));

    // 写入脚本数据
    written = write(fd, buf, info.mem_size);

    // 写入字符串池
    written = write(fd, string_pool_base, info.str_size);

    close(fd);

    // 清理
    efree(buf);
    zend_shared_alloc_destroy_xlat_table();

    return SUCCESS;
}
```

---

## 6. 运行时加载流程

### 6.1 加载流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                     运行时加载流程                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  .phpc File                                                      │
│       │                                                          │
│       ▼                                                          │
│  ┌──────────────────┐                                           │
│  │ 1. 读取文件       │  opkit_compile_script_load()             │
│  │    - 读取 metainfo│  - open() + read()                        │
│  │    - 验证魔数     │  - 验证 "PHPC" magic                      │
│  │    - 验证系统 ID  │  - 对比 opkit_system_id                   │
│  │    - 验证校验和   │  - Adler-32 校验                          │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 2. 分配内存       │  emalloc() 或 opkit_shared_alloc()        │
│  │    - 堆内存路径   │  - emalloc(mem_size + str_size)           │
│  │    - SHM 路径     │  - opkit_shared_alloc(total_size)         │
│  │    - 复制数据     │  - memcpy 从文件到内存                     │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 3. 反序列化       │  zend_file_cache_unserialize*             │
│  │    - 偏移量转指针 │  - UNSERIALIZE_PTR                        │
│  │    - 字符串还原   │  - zend_file_cache_unserialize_interned   │
│  │    - 重建结构     │  - 递归处理所有子结构                      │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 4. 注册到引擎     │  opkit_boot()                             │
│  │    - 注册函数     │  - CG(function_table)                     │
│  │    - 注册类       │  - CG(class_table)                        │
│  │    - 注册常量     │  - EG(zend_constants)                     │
│  │    - 类链接       │  - opkit_link_classes()                   │
│  │    - 早期绑定     │  - zend_accel_do_delayed_early_binding    │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 5. 执行脚本       │  zend_execute()                           │
│  │    - 准备运行时   │  - 初始化 runtime cache                   │
│  │    - 执行入口     │  - 执行 main_op_array                     │
│  │    - 处理返回值   │  - 返回执行结果                            │
│  └──────────────────┘                                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 文件加载代码

```c
// opkit_compile.c: opkit_compile_script_load()
zend_persistent_script *opkit_compile_script_load(zend_string *filename) {
    zend_file_cache_metainfo info;
    zend_persistent_script *script;
    void *mem, *buf;
    int fd;
    ssize_t read_bytes;
    unsigned int checksum;

    // 打开文件
    fd = opkit_compile_open(ZSTR_VAL(filename), O_RDONLY|O_BINARY, 0);

    // 读取文件头
    read_bytes = read(fd, &info, sizeof(info));

    // 验证魔数
    if (memcmp(info.magic, "PHPC", 5) != 0) {
        close(fd);
        return NULL;
    }

    // 验证系统 ID
    if (!zend_string_equals(info.system_id, opkit_system_id)) {
        close(fd);
        return NULL;
    }

    // 分配内存
    size_t total_size = info.mem_size + info.str_size;
    // 当 opkit.shm_size > 0 且共享内存有足够空间时，使用 SHM 路径：
    // mem = opkit_shared_alloc(total_size);
    // 否则回退到堆内存路径：
    mem = emalloc(total_size);

    // 读取脚本数据
    buf = mem;
    read_bytes = read(fd, buf, info.mem_size);

    // 读取字符串池
    string_pool_base = (char*)buf + info.mem_size;
    read_bytes = read(fd, string_pool_base, info.str_size);

    close(fd);

    // 验证校验和
    checksum = zend_adler32(0, (unsigned char*)buf, info.mem_size);
    checksum = zend_adler32(checksum, (unsigned char*)string_pool_base, info.str_size);
    if (checksum != info.checksum) {
        efree(mem);
        return NULL;
    }

    // 设置脚本指针
    script = (zend_persistent_script*)buf;
    script->mem = buf;

    // 反序列化（内含指针修复）
    zend_file_cache_unserialize(script, &info, buf);

    return script;
}
```

### 6.3 反序列化详解

```c
// 反序列化主函数
static void zend_file_cache_unserialize(zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
    // 反序列化文件名
    UNSERIALIZE_STR(script->script.filename);

    // 反序列化类表
    zend_file_cache_unserialize_hash(&script->script.class_table, script, buf,
                                     zend_file_cache_unserialize_class, NULL);

    // 反序列化函数表
    zend_file_cache_unserialize_hash(&script->script.function_table, script, buf,
                                     zend_file_cache_unserialize_func, NULL);

    // 反序列化主 op_array
    zend_file_cache_unserialize_op_array(&script->script.main_op_array, script, buf);

    // 反序列化常量表
    zend_file_cache_unserialize_hash(&((opkit_persistent_script*)script)->constants_table,
                                     script, buf, zend_file_cache_unserialize_constant, NULL);

    // 反序列化警告
    zend_file_cache_unserialize_warnings(script, buf);

    // 反序列化早期绑定
    zend_file_cache_unserialize_early_bindings(script, buf);
}

// 字符串反序列化
#define UNSERIALIZE_STR(str) do { \
    if (IS_SERIALIZED(str) || IS_SERIALIZED_INTERNED(str)) { \
        str = zend_file_cache_unserialize_interned(str); \
    } \
} while (0)
```

### 6.4 指针修复

指针修复在反序列化过程中内联完成（`zend_file_cache_unserialize_op_array`），无需单独的修复阶段：

```c
// 在反序列化 op_array 时自动修复
while (opline < end) {
#if ZEND_USE_ABS_CONST_ADDR
    if (opline->op1_type == IS_CONST) {
        UNSERIALIZE_PTR(opline->op1.zv);
    }
    if (opline->op2_type == IS_CONST) {
        UNSERIALIZE_PTR(opline->op2.zv);
    }
#endif
#if ZEND_USE_ABS_JMP_ADDR
    switch (opline->opcode) {
        case ZEND_JMP:
        case ZEND_FAST_CALL:
            UNSERIALIZE_PTR(opline->op1.jmp_addr);
            break;
        case ZEND_JMPZ:
        case ZEND_JMPNZ:
            // ...
            UNSERIALIZE_PTR(opline->op2.jmp_addr);
            break;
    }
#endif
    opline++;
}
```

---

## 7. 关键数据结构转换

### 7.1 类结构转换

```
编译前 (zend_class_entry):
┌─────────────────────────────────────┐
│ name ──────────────► zend_string    │
│ parent_name ───────► zend_string    │
│ function_table ────► HashTable      │
│   ├── method1 ─────► zend_op_array  │
│   └── method2 ─────► zend_op_array  │
│ properties_info ───► HashTable      │
│   ├── prop1 ───────► zend_property  │
│ default_properties_table ──► zval[] │
│ ...                                 │
└─────────────────────────────────────┘

序列化后 (文件中的偏移量):
┌─────────────────────────────────────┐
│ name ──────────────► 0x0010 (偏移)  │
│ parent_name ───────► 0x0028 (偏移)  │
│ function_table ────► HashTable      │
│   ├── method1 ─────► 0x0100 (偏移)  │
│ properties_info ───► HashTable      │
│ ...                                 │
└─────────────────────────────────────┘

加载后 (内存中的指针):
┌─────────────────────────────────────┐
│ name ──────────────► 0x7f...010     │
│ parent_name ───────► 0x7f...028     │
│ function_table ────► HashTable      │
│   ├── method1 ─────► 0x7f...100     │
│ ...                                 │
└─────────────────────────────────────┘
```

### 7.2 函数/方法结构转换

```c
// 持久化前后的 zend_op_array 变化
typedef struct _zend_op_array {
    // 编译时状态
    zend_uchar type;                    // ZEND_USER_FUNCTION
    zend_uchar arg_flags[3];
    uint32_t fn_flags;

    // 需要持久化的指针
    zend_string *function_name;         // SERIALIZE_STR
    zend_string *filename;              // SERIALIZE_STR
    zend_op *opcodes;                   // SERIALIZE_PTR + 内容复制
    zval *literals;                     // SERIALIZE_PTR + 内容复制
    zend_string **vars;                 // SERIALIZE_PTR + 每个元素序列化
    zend_arg_info *arg_info;            // SERIALIZE_PTR + 内容复制
    HashTable *static_variables;        // SERIALIZE_PTR + 递归序列化
    zend_live_range *live_range;        // SERIALIZE_PTR
    zend_try_catch_element *try_catch_array;  // SERIALIZE_PTR

    // 运行时初始化标志
    ZEND_MAP_PTR(void **, static_variables_ptr);
    ZEND_MAP_PTR(void **, run_time_cache);
    void **run_time_cache__ptr;         // 转换为相对于 mem 的偏移

    // ... 其他字段
} zend_op_array;
```

### 7.3 常量表转换

```c
// opkit 扩展的常量表存储
typedef struct _opkit_persistent_script {
    zend_persistent_script script;       // 标准 Zend 脚本结构
    HashTable constants_table;           // 额外的常量表
} opkit_persistent_script;

// 常量结构
struct _zend_constant {
    zval value;                          // 常量值（需要序列化）
    zend_string *name;                   // 常量名（SERIALIZE_STR）
    int flags;                           // CONST_PERSISTENT | CONST_CS
    int module_number;
};

// 序列化过程
static void zend_file_cache_serialize_constant(zval *zv, ...)
{
    zend_constant *zc;
    SERIALIZE_PTR(Z_PTR_P(zv));          // 序列化常量结构指针
    zc = Z_PTR_P(zv);
    zc = UNSERIALIZED_PTR(zc);
    if (zc) {
        if (zc->name) {
            SERIALIZE_STR(zc->name);     // 序列化名称
        }
        zend_file_cache_serialize_zval(&zc->value, ...);  // 序列化值
    }
}
```

---

## 8. PHP 8.4 特殊处理

### 8.1 Property Hooks 处理

PHP 8.4 引入了 Property Hooks，需要特殊处理：

```c
// 序列化 Property Hooks
#if PHP_VERSION_ID >= 80400
static void zend_file_cache_serialize_prop_info(zval *zv, ...)
{
    // ... 其他字段序列化

    // 序列化 prototype 指针
    SERIALIZE_PTR(prop->prototype);

    // 序列化 hooks
    if (prop->hooks) {
        zend_function **hooks = prop->hooks;
        SERIALIZE_PTR(prop->hooks);           // 序列化 hooks 数组指针
        hooks = UNSERIALIZED_PTR(hooks);

        // 序列化每个 hook 函数
        for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
            if (hooks[i]) {
                SERIALIZE_PTR(hooks[i]);       // 序列化单个 hook 指针
                zend_function *hook = hooks[i];
                hook = UNSERIALIZED_PTR(hook);
                zend_file_cache_serialize_op_array(&hook->op_array, ...);
            }
        }
    }
}
#endif

// 持久化 Property Hooks
#if PHP_VERSION_ID >= 80400
static zend_property_info *zend_persist_property_info(zend_property_info *prop)
{
    // ... 其他字段处理

    // 持久化 prototype
    if (copy->prototype) {
        copy->prototype = zend_shared_alloc_get_xlat_entry((void *)copy->prototype);
    }

    // 持久化 hooks
    if (copy->hooks) {
        zend_function **hooks = copy->hooks;
        copy->hooks = _opkit_shared_memdup_put_md(hooks, ZEND_PROPERTY_HOOK_STRUCT_SIZE);

        for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
            if (copy->hooks[i]) {
                zend_op_array *hook = (zend_op_array *)copy->hooks[i];
                hook = zend_shared_memdup_put(hook, sizeof(zend_op_array));

                // 关键：设置 prop_info 指针
                hook->prop_info = copy;

                zend_persist_op_array_ex(hook, ZCG(current_persistent_script));
                copy->hooks[i] = (zend_function *)hook;
            }
        }
    }
    return copy;
}
#endif
```

### 8.2 doc_comment 版本处理

PHP 8.4 从 `zend_property_info` 中移除了 `doc_comment`，但 `zend_op_array`、`zend_class_constant` 和 `zend_class_entry` 仍保留该字段（`zend_class_entry` 的 `doc_comment` 在 8.4+ 被移出了 `info.user` union，直接位于结构体中）。

```c
// op_array / class_constant —— 所有版本均保留 doc_comment
if (op_array->doc_comment) {
    zend_accel_store_interned_string(op_array->doc_comment);
}

// class_entry —— 按版本区分位置
#if PHP_VERSION_ID >= 80400
    if (ce->doc_comment) {
        zend_accel_store_interned_string(ce->doc_comment);
    }
#else
    if (ce->info.user.doc_comment) {
        zend_accel_store_interned_string(ce->info.user.doc_comment);
    }
#endif

// property_info —— 仅在 8.2/8.3 中保留
#if PHP_VERSION_ID < 80400
    if (copy->doc_comment) {
        zend_accel_store_interned_string(copy->doc_comment);
    }
#endif
```

### 8.3 运行时缓存处理

对于使用堆分配 runtime cache 的 PHP 版本，需要在 RSHUTDOWN 时清理以防止内存泄漏：

```c
static void opkit_reset_script(void) {
    opkit_script_node *node = loaded_scripts;
    opkit_script_node *next;

    while (node) {
        next = node->next;
        if (node->script) {
            opkit_clean_script_items(node->script);

            /* Clean up heap allocated runtime cache to prevent memory leaks
             * This is needed for all PHP versions that use heap allocation for runtime cache
             */
            if (node->executed) {
                zend_op_array *main_op_array = &node->script->script.main_op_array;
                if (main_op_array->fn_flags & ZEND_ACC_HEAP_RT_CACHE) {
                    void *cache = ZEND_MAP_PTR(main_op_array->run_time_cache);
                    if (cache) {
                        efree(cache);
                        ZEND_MAP_PTR(main_op_array->run_time_cache) = NULL;
                    }
                }
            }
        }
        if (node->mem_to_free) {
            efree(node->mem_to_free);
        }
        efree(node);
        node = next;
    }
    loaded_scripts = NULL;
}
```

---

## 9. 调试和诊断

### 9.1 内存布局信息

```php
// 使用 opkit_get_info() 获取编译后文件的信息
$info = opkit_get_info('script.phpc');

// 返回的信息结构：
[
    'magic' => 'PHPC',
    'system_id' => '...',
    'system_id_match' => true,
    'mem_size' => 12345,
    'str_size' => 5678,
    'metadata_size' => 3000,
    'code_size' => 5000,
    'data_size' => 4000,
    'misc_size' => 345,
    'functions' => [...],
    'classes' => [...],
    'constants' => [...],
    'structure' => [...]  // 详细的内存布局
]
```

### 9.2 编译时调试

```c
// 添加调试输出
#define OPKIT_DEBUG 1

#ifdef OPKIT_DEBUG
#define debug_log(fmt, ...) fprintf(stderr, "[OPKIT] " fmt "\n", ##__VA_ARGS__)
#else
#define debug_log(fmt, ...)
#endif

// 在关键位置使用
debug_log("Serializing op_array: %s", ZSTR_VAL(op_array->function_name));
debug_log("Memory used: metadata=%zu, code=%zu, data=%zu, misc=%zu",
          opkit_metadata_size, opkit_code_size, opkit_data_size, opkit_misc_size);
```

---

## 10. 共享内存运行时加载

### 10.1 SHM 加载路径

当 `opkit.shm_size` INI 配置大于 0 时，OpKit 会在 MINIT 阶段通过 `mmap(MAP_SHARED | MAP_ANONYMOUS)` 创建一块进程间共享内存。运行时加载 `.phpc` 文件时，会优先尝试将脚本数据持久化到共享内存中：

```
┌─────────────────────────────────────────────────────────────────┐
│                   SHM Runtime Loading Path                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  opkit_load($phpc)                                               │
│       │                                                          │
│       ▼                                                          │
│  ┌──────────────────┐                                           │
│  │ 1. 读取 .phpc 文件 │  opkit_compile_script_load()            │
│  │    - 验证头信息    │  - 与标准加载流程相同                    │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 2. 选择内存目标   │  根据 opkit.shm_size 决定                 │
│  │    ├── SHM 路径   │  - opkit_shared_alloc(total_size)        │
│  │    │              │  - 数据对所有 fork 子进程可见            │
│  │    └── 堆路径     │  - emalloc(total_size)                   │
│  │                   │  - 仅当前进程可用                        │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 3. 持久化到目标   │  zend_accel_script_persist()             │
│  │    - 复制脚本结构  │  - 使用 ZCG(mem) 作为分配基址            │
│  │    - 设置 in_shm  │  - opkit_keep_memory() 标记 in_shm       │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 4. 注册符号       │  opkit_boot()                            │
│  │    - 函数/类/常量  │  - 每个进程独立执行                      │
│  │    - 类链接       │  - Zend Engine 表是进程私有的            │
│  └────────┬─────────┘                                           │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐                                           │
│  │ 5. fork 子进程    │  继承父进程的 SHM 映射                   │
│  │    - 无需重新加载  │  - 物理内存共享                          │
│  │    - 直接执行 boot│  - 符号注册仍需独立执行                  │
│  └──────────────────┘                                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 10.2 SHM 重置 (`opkit_shm_reset`)

`opkit_shm_reset()` 用于清空共享内存中的所有缓存脚本：

```c
// C 实现
bool opkit_shm_reset(void) {
    if (!opkit_shm_segment) {
        return false;
    }
    opkit_shared_alloc_lock();      // 获取进程间写锁
    opkit_shm_segment->pos = 0;     // 重置分配偏移量
    opkit_shared_alloc_unlock();    // 释放锁
    return true;
}
```

**PHP 层行为**：
```php
bool opkit_shm_reset();
```

PHP 层的 `opkit_shm_reset()` 在调用 C API 之前会先执行 `opkit_reset_script()`，清理当前进程已注册的符号和 `loaded_scripts` 链表。这是必要的安全措施，防止 reset 后出现悬空指针访问已释放的共享内存。

**使用场景**：
- 开发调试时快速清除缓存
- 部署新版本前重置旧缓存
- 内存压力较大时手动回收

**注意事项**：
- 重置后所有已加载的脚本需要重新调用 `opkit_load()`
- 若其他进程正在使用共享内存中的脚本数据，重置可能导致这些进程崩溃（应在所有进程协调后执行）
- 带 `fcntl` 文件锁保护，避免并发重置导致数据不一致

### 10.3 SHM 统计信息 (`opkit_shm_stat`)

```php
?array opkit_shm_stat();
```

返回共享内存的使用统计：

```php
[
    'shm_size' => 33554432,  // 总容量（由 opkit.shm_size 设置）
    'free'     => 33072000,  // 剩余可用空间
    'used'     => 482432,    // 已使用空间
]
```

若共享内存未初始化（`opkit.shm_size = 0`），返回 `null`。

---

## 11. 总结

OpKit 的编译流程核心要点：

1. **两阶段编译**：先由 PHP 编译为 Opcodes，再由 OpKit 持久化为二进制文件

2. **四区内存模型**：Metadata、Code、Data、Misc 分区存储，优化缓存效率

3. **指针序列化**：将内存指针转换为相对偏移量，实现跨进程/重启后的正确加载

4. **字符串池化**：所有字符串统一存储在字符串池，减少重复，支持驻留字符串

5. **xlat_table**：避免重复复制相同结构，处理循环引用

6. **系统 ID 验证**：确保 .phpc 文件只能在兼容的 PHP 版本和架构上运行

7. **早期绑定处理**：处理类继承关系，延迟到运行时解析

8. **共享内存支持**：通过 `mmap(MAP_SHARED)` 实现多进程间的脚本数据共享，降低内存占用和启动延迟