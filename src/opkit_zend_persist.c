/*
   +----------------------------------------------------------------------+
   | OpKit                                                                |
   +----------------------------------------------------------------------+
   | Copyright (c) The PHP Group                                          |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | https://www.php.net/license/3_01.txt                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Eno-CN <Eno_CN@qq.com>                                       |
   +----------------------------------------------------------------------+
   | OpKit is heavily based on Zend OPcache                               |
   +----------------------------------------------------------------------+
   | This product includes PHP software, freely available from            |
   | <http://www.php.net/software/>                                       |
   +----------------------------------------------------------------------+
*/


#include "php.h"
#include "opkit_wrapper.h"

#include "zend_extensions.h"
#include "zend_vm.h"
#include "zend_constants.h"
#include "zend_operators.h"
#include "zend_interfaces.h"
#include "zend_attributes.h"

#if PHP_VERSION_ID < 80300
#ifndef ZSTR_IS_VALID_UTF8
#define ZSTR_IS_VALID_UTF8(str) 0
#endif
#ifndef IS_STR_VALID_UTF8
#define IS_STR_VALID_UTF8 0
#endif
#endif

size_t opkit_metadata_size = 0;
size_t opkit_code_size = 0;
size_t opkit_data_size = 0;
size_t opkit_misc_size = 0;

#define zend_set_str_gc_flags(str) do { \
	GC_SET_REFCOUNT(str, 0); \
	uint32_t flags = GC_STRING | (ZSTR_IS_VALID_UTF8(str) ? IS_STR_VALID_UTF8 : 0); \
	flags |= ((IS_STR_INTERNED | IS_STR_PERMANENT) << GC_FLAGS_SHIFT); \
	GC_TYPE_INFO(str) = flags; \
} while (0)

#define zend_accel_store_string(str) do { \
		if (str) { \
			zend_string *new_str = zend_shared_alloc_get_xlat_entry(str); \
			if (new_str) { \
				zend_string_release_ex(str, 0); \
				str = new_str; \
			} else { \
				new_str = _opkit_shared_memdup_put_dt((void*)str, _ZSTR_STRUCT_SIZE(ZSTR_LEN(str))); \
				zend_string_release_ex(str, 0); \
				str = new_str; \
				zend_string_hash_val(str); \
				zend_set_str_gc_flags(str); \
			} \
		} \
	} while (0)

#define zend_accel_memdup_string(str) do { \
		if (str) { \
			zend_string *new_str = zend_shared_alloc_get_xlat_entry(str); \
			if (new_str) { \
				str = new_str; \
			} else { \
				new_str = _opkit_shared_memdup_put_dt((void*)str, _ZSTR_STRUCT_SIZE(ZSTR_LEN(str))); \
				str = new_str; \
				zend_string_hash_val(str); \
				zend_set_str_gc_flags(str); \
			} \
		} \
	} while (0)

#define zend_accel_store_interned_string(str) do { \
		if (str) { \
			if (!IS_ACCEL_INTERNED(str)) { \
				zend_accel_store_string(str); \
			} else { \
				GC_SET_REFCOUNT(str, 0); \
			} \
		} \
	} while (0)

#define zend_accel_memdup_interned_string(str) do { \
		if (str) { \
			if (!IS_ACCEL_INTERNED(str)) { \
				zend_accel_memdup_string(str); \
			} else { \
				GC_SET_REFCOUNT(str, 0); \
			} \
		} \
	} while (0)

static opkit_persistent_script *old_script_ptr = NULL;

static void zend_persist_zval(zval *z);
static void zend_persist_op_array(zval *zv);

static void zend_hash_persist(HashTable *ht)
{
	uint32_t idx, nIndex;
	Bucket *p;

	HT_FLAGS(ht) |= HASH_FLAG_STATIC_KEYS;
	ht->pDestructor = NULL;
	ht->nInternalPointer = 0;

	if (HT_FLAGS(ht) & HASH_FLAG_UNINITIALIZED) {
		HT_SET_DATA_ADDR(ht, (void*)&uninitialized_bucket);
		return;
	}
	if (ht->nNumUsed == 0) {
		efree(HT_GET_DATA_ADDR(ht));
		ht->nTableMask = HT_MIN_MASK;
		HT_SET_DATA_ADDR(ht, (void*)&uninitialized_bucket);
		HT_FLAGS(ht) |= HASH_FLAG_UNINITIALIZED;
		return;
	}

	void *old_data = HT_GET_DATA_ADDR(ht);
	void *new_data = zend_shared_memdup_put(HT_GET_DATA_ADDR(ht), HT_USED_SIZE(ht));
	efree(old_data);
	HT_SET_DATA_ADDR(ht, new_data);

	if (HT_IS_PACKED(ht)) {
		return;
	}

	/* update the hash table data */
	nIndex = ht->nTableMask;
	do {
		HT_HASH(ht, nIndex) = HT_INVALID_IDX;
	} while (++nIndex != 0);

	for (idx = 0; idx < ht->nNumUsed; idx++) {
		p = ht->arData + idx;
		if (Z_TYPE(p->val) == IS_UNDEF) continue;

		nIndex = p->h | ht->nTableMask;
		Z_NEXT(p->val) = HT_HASH(ht, nIndex);
		HT_HASH(ht, nIndex) = HT_IDX_TO_HASH(idx);
	}
}

static zend_ast *zend_persist_ast(zend_ast *ast)
{
	uint32_t i;

	if (ast->kind == ZEND_AST_ZVAL || ast->kind == ZEND_AST_CONSTANT) {
		zend_ast_zval *copy = zend_shared_memdup_put_free(ast, sizeof(zend_ast_zval));
		zend_persist_zval(&copy->val);
		ast = (zend_ast *)copy;
	} else if (zend_ast_is_list(ast)) {
		zend_ast_list *list = zend_ast_get_list(ast);
		zend_ast_list *copy = zend_shared_memdup_put_free(ast, sizeof(zend_ast_list) + sizeof(zend_ast *) * (list->children - 1));
		for (i = 0; i < list->children; i++) {
			if (copy->child[i]) {
				copy->child[i] = zend_persist_ast(copy->child[i]);
			}
		}
		ast = (zend_ast *)copy;
	} else {
		uint32_t children = zend_ast_get_num_children(ast);
		zend_ast *copy = zend_shared_memdup_put_free(ast, sizeof(zend_ast) + sizeof(zend_ast *) * (children - 1));
		for (i = 0; i < children; i++) {
			if (copy->child[i]) {
				copy->child[i] = zend_persist_ast(copy->child[i]);
			}
		}
		ast = copy;
	}
	return ast;
}

static void zend_persist_zval(zval *z)
{
	void *new_ptr;

	switch (Z_TYPE_P(z)) {
		case IS_STRING:
			zend_accel_store_interned_string(Z_STR_P(z));
			Z_TYPE_FLAGS_P(z) = 0;
			break;
		case IS_ARRAY:
			new_ptr = zend_shared_alloc_get_xlat_entry(Z_ARR_P(z));
			if (new_ptr) {
				Z_ARR_P(z) = new_ptr;
				Z_TYPE_FLAGS_P(z) = 0;
			} else {
				HashTable *ht;

				if (!Z_REFCOUNTED_P(z)) {
					ht = zend_shared_memdup_put(Z_ARR_P(z), sizeof(zend_array));
				} else {
					GC_REMOVE_FROM_BUFFER(Z_ARR_P(z));
					ht = zend_shared_memdup_put_free(Z_ARR_P(z), sizeof(zend_array));
				}
				Z_ARR_P(z) = ht;
				zend_hash_persist(ht);
				if (HT_IS_PACKED(ht)) {
					zval *zv;
					ZEND_HASH_PACKED_FOREACH_VAL(ht, zv) {
						zend_persist_zval(zv);
					} ZEND_HASH_FOREACH_END();
				} else {
					Bucket *p;
					ZEND_HASH_MAP_FOREACH_BUCKET(ht, p) {
						if (p->key) {
							zend_accel_store_interned_string(p->key);
						}
						zend_persist_zval(&p->val);
					} ZEND_HASH_FOREACH_END();
				}
				Z_TYPE_FLAGS_P(z) = 0;
				GC_SET_REFCOUNT(Z_COUNTED_P(z), 2);
				GC_ADD_FLAGS(Z_COUNTED_P(z), IS_ARRAY_IMMUTABLE);
			}
			break;
		case IS_CONSTANT_AST:
			new_ptr = zend_shared_alloc_get_xlat_entry(Z_AST_P(z));
			if (new_ptr) {
				Z_AST_P(z) = new_ptr;
				Z_TYPE_FLAGS_P(z) = 0;
			} else {
				zend_ast_ref *old_ref = Z_AST_P(z);
				zend_ast_ref *new_ref = zend_shared_memdup_put(old_ref, sizeof(zend_ast_ref));
				Z_AST_P(z) = new_ref;
				((zend_ast_ref*)(new_ref))->gc.u.type_info = (uintptr_t)zend_persist_ast((zend_ast*)(uintptr_t)old_ref->gc.u.type_info);
				Z_TYPE_FLAGS_P(z) = 0;
				GC_SET_REFCOUNT(new_ref, 1);
				GC_ADD_FLAGS(new_ref, GC_IMMUTABLE);
				efree(old_ref);
			}
			break;
	default:
		/* IS_UNDEF, IS_NULL, IS_FALSE, IS_TRUE, IS_LONG, IS_DOUBLE, IS_RESOURCE */
		break;
	}
}

static HashTable *zend_persist_attributes(HashTable *attributes)
{
	uint32_t i;
	zval *v;

	HashTable *xlat = zend_shared_alloc_get_xlat_entry(attributes);
	if (xlat) {
		return xlat;
	}

	zend_hash_persist(attributes);

	ZEND_HASH_PACKED_FOREACH_VAL(attributes, v) {
		zend_attribute *attr = Z_PTR_P(v);
		zend_attribute *copy = zend_shared_memdup_put_free(attr, ZEND_ATTRIBUTE_SIZE(attr->argc));

		zend_accel_store_interned_string(copy->name);
		zend_accel_store_interned_string(copy->lcname);

		for (i = 0; i < copy->argc; i++) {
			if (copy->args[i].name) {
				zend_accel_store_interned_string(copy->args[i].name);
			}
			zend_persist_zval(&copy->args[i].value);
		}
		ZVAL_PTR(v, copy);
	} ZEND_HASH_FOREACH_END();

	HashTable *ptr = zend_shared_memdup_put_free(attributes, sizeof(HashTable));
	GC_SET_REFCOUNT(ptr, 2);
	GC_TYPE_INFO(ptr) = GC_ARRAY | ((IS_ARRAY_IMMUTABLE|GC_NOT_COLLECTABLE) << GC_FLAGS_SHIFT);

	return ptr;
}

static void zend_persist_type(zend_type *type)
{
	if (ZEND_TYPE_HAS_LIST(*type)) {
		zend_type_list *old_list = ZEND_TYPE_LIST(*type);
		zend_type_list *new_list = _opkit_shared_memdup_put_free_ms(old_list, ZEND_TYPE_LIST_SIZE(old_list->num_types));
		for (uint32_t i = 0; i < new_list->num_types; i++) {
			zend_persist_type(&new_list->types[i]);
		}
		type->ptr = new_list;
	} else if (ZEND_TYPE_HAS_NAME(*type)) {
		zend_string *name = ZEND_TYPE_NAME(*type);
		zend_accel_store_interned_string(name);
		type->ptr = name;
		if (!ZCG(current_persistent_script)->corrupted) {
			zend_accel_get_class_name_map_ptr(name);
		}
	}
}

static void zend_persist_op_array_ex(zend_op_array *op_array, zend_persistent_script *main_persistent_script)
{
	if (op_array->type != ZEND_USER_FUNCTION) {
		return;
	}

	if (op_array->refcount && --(*op_array->refcount) == 0) {
		efree(op_array->refcount);
	}
	op_array->refcount = NULL;

	if (op_array->scope) {
		op_array->scope = zend_shared_alloc_get_xlat_entry(op_array->scope);
	}

	zend_accel_store_interned_string(op_array->function_name);
	if (op_array->filename) {
		zend_accel_store_interned_string(op_array->filename);
	}

#if PHP_VERSION_ID < 80400
	if (op_array->doc_comment) {
		zend_accel_store_interned_string(op_array->doc_comment);
	}
#endif

	if (op_array->arg_info) {
		zend_arg_info *arg_info = op_array->arg_info;
		uint32_t num_args = op_array->num_args;
		if (op_array->fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
			arg_info--;
			num_args++;
		}
		if (op_array->fn_flags & ZEND_ACC_VARIADIC) {
			num_args++;
		}
		zend_arg_info *copy = _opkit_shared_memdup_put_free_dt(arg_info, sizeof(zend_arg_info) * num_args);
		for (uint32_t i = 0; i < num_args; i++) {
			if (copy[i].name) {
				zend_accel_store_interned_string(copy[i].name);
			}
			zend_persist_type(&copy[i].type);
		}
		op_array->arg_info = (op_array->fn_flags & ZEND_ACC_HAS_RETURN_TYPE) ? copy + 1 : copy;
	}

	if (op_array->live_range) {
		void *old_ptr = op_array->live_range;
		op_array->live_range = _opkit_shared_memdup_put_free_ms(old_ptr, sizeof(zend_live_range) * op_array->last_live_range);
	}

	if (op_array->try_catch_array) {
		void *old_ptr = op_array->try_catch_array;
		op_array->try_catch_array = _opkit_shared_memdup_put_free_ms(old_ptr, sizeof(zend_try_catch_element) * op_array->last_try_catch);
	}

	if (op_array->vars) {
		void *old_ptr = op_array->vars;
		zend_string **copy = _opkit_shared_memdup_put_free_dt(old_ptr, sizeof(zend_string *) * op_array->last_var);
		for (int i = 0; i < op_array->last_var; i++) {
			zend_accel_store_interned_string(copy[i]);
		}
		op_array->vars = copy;
	}

	if (op_array->opcodes) {
		void *old_ptr = op_array->opcodes;
		zend_op *new_opcodes = _opkit_shared_memdup_put_free_cd(old_ptr, sizeof(zend_op) * op_array->last);
#if ZEND_USE_ABS_CONST_ADDR
		for (uint32_t i = 0; i < op_array->last; i++) {
			zend_op *opline = &new_opcodes[i];
			if (opline->op1_type == IS_CONST) {
				opline->op1.zv = (zval*)((char*)opline->op1.zv + ((char*)new_opcodes - (char*)op_array->opcodes));
			}
			if (opline->op2_type == IS_CONST) {
				opline->op2.zv = (zval*)((char*)opline->op2.zv + ((char*)new_opcodes - (char*)op_array->opcodes));
			}
		}
#endif
		op_array->opcodes = new_opcodes;
	}

	if (op_array->literals) {
		void *old_ptr = op_array->literals;
		zval *copy = _opkit_shared_memdup_put_dt(old_ptr, sizeof(zval) * op_array->last_literal);
		for (int i = 0; i < op_array->last_literal; i++) {
			if (Z_TYPE_P(&copy[i]) == IS_STRING) {
				zend_string *s = Z_STR(copy[i]);
				zend_accel_store_interned_string(s);
				Z_STR(copy[i]) = s;
			} else {
				zend_persist_zval(&copy[i]);
			}
		}
		op_array->literals = copy;
	}

	if (op_array->static_variables) {
		HashTable *old_ht = op_array->static_variables;
		HashTable *new_ht = _opkit_shared_memdup_put_md(old_ht, sizeof(HashTable));
		zend_hash_persist(new_ht);
		Bucket *p;
		ZEND_HASH_MAP_FOREACH_BUCKET(new_ht, p) {
			if (p->key) {
				zend_accel_store_interned_string(p->key);
			}
			zend_persist_zval(&p->val);
		} ZEND_HASH_FOREACH_END();
		op_array->static_variables = new_ht;
	}

	if (op_array->attributes) {
		op_array->attributes = zend_persist_attributes(op_array->attributes);
	}

	if (op_array->num_dynamic_func_defs) {
		zend_op_array **old_defs = op_array->dynamic_func_defs;
		zend_op_array **new_defs = _opkit_shared_memdup_put_md(old_defs, sizeof(zend_op_array*) * op_array->num_dynamic_func_defs);
		for (uint32_t i = 0; i < op_array->num_dynamic_func_defs; i++) {
			zend_op_array *old_op_array = old_defs[i];
			zend_op_array *new_op_array = _opkit_shared_memdup_put_md(old_op_array, sizeof(zend_op_array));
			zend_shared_alloc_register_xlat_entry(old_op_array, new_op_array);
			zend_persist_op_array_ex(new_op_array, main_persistent_script);
			new_defs[i] = new_op_array;
		}
		op_array->dynamic_func_defs = new_defs;
	}

	if (op_array->cache_size) {
		memset(ZCG(mem), 0, op_array->cache_size);
		if (main_persistent_script) {
			op_array->run_time_cache__ptr = (void**)(uintptr_t)((char*)ZCG(mem) - (char*)main_persistent_script->mem);
		} else if (ZCG(current_persistent_script)) {
			op_array->run_time_cache__ptr = (void**)(uintptr_t)((char*)ZCG(mem) - (char*)ZCG(current_persistent_script)->mem);
		} else {
			op_array->run_time_cache__ptr = NULL;
		}
		ZCG(mem) = (void*)((char*)ZCG(mem) + op_array->cache_size);
	} else {
		op_array->run_time_cache__ptr = NULL;
	}
}

static void zend_persist_op_array(zval *zv)
{
	zend_op_array *op_array = Z_PTR_P(zv);
	zend_op_array *copy;

	if (op_array->type != ZEND_USER_FUNCTION) {
		return;
	}

	copy = zend_shared_alloc_get_xlat_entry(op_array);
	if (copy) {
		ZVAL_PTR(zv, copy);
		return;
	}

	copy = _opkit_shared_memdup_put_md(op_array, sizeof(zend_op_array));
	if (op_array != &ZCG(current_persistent_script)->script.main_op_array) {
		zend_shared_alloc_register_xlat_entry(op_array, copy);
		zend_persist_op_array_ex(copy, ZCG(current_persistent_script));
	} else {
		zend_shared_alloc_register_xlat_entry(&old_script_ptr->script.script.main_op_array, copy);
		zend_persist_op_array_ex(copy, ZCG(current_persistent_script));
	}
	ZVAL_PTR(zv, copy);
}

static void zend_persist_class_method(zval *zv, zend_class_entry *ce)
{
	zend_function *fn = Z_PTR_P(zv);
	zend_function *copy;

	if (fn->type != ZEND_USER_FUNCTION) {
		return;
	}

	if (fn->common.scope != ce) {
		copy = zend_shared_alloc_get_xlat_entry(fn);
		if (copy) {
			ZVAL_PTR(zv, copy);
			return;
		}
	}

	copy = _opkit_shared_memdup_put_md(fn, sizeof(zend_op_array));
	zend_persist_op_array_ex(&copy->op_array, ZCG(current_persistent_script));
	if (fn->common.scope == ce) {
		zend_shared_alloc_register_xlat_entry(fn, copy);
	}
	ZVAL_PTR(zv, copy);
}

static zend_property_info *zend_persist_property_info(zend_property_info *prop)
{
	zend_property_info *copy = zend_shared_alloc_get_xlat_entry(prop);
	if (copy) return copy;

	copy = _opkit_shared_memdup_put_md(prop, sizeof(zend_property_info));
	zend_accel_store_interned_string(copy->name);
#if PHP_VERSION_ID < 80400
	if (copy->doc_comment) {
		zend_accel_store_interned_string(copy->doc_comment);
	}
#endif
	zend_persist_type(&copy->type);
	if (copy->attributes) {
		copy->attributes = zend_persist_attributes(copy->attributes);
	}
#if PHP_VERSION_ID >= 80400
	/* Persist property prototype */
	if (copy->prototype) {
		/* The prototype will be persisted later when processing the parent class's properties */
		copy->prototype = zend_shared_alloc_get_xlat_entry((void *)copy->prototype);
	}
	/* Persist property hooks */
	if (copy->hooks) {
		zend_function **hooks = copy->hooks;
		copy->hooks = _opkit_shared_memdup_put_md(hooks, ZEND_PROPERTY_HOOK_STRUCT_SIZE);
		for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
			if (copy->hooks[i]) {
				zend_op_array *hook = (zend_op_array *)copy->hooks[i];
				hook = zend_shared_memdup_put(hook, sizeof(zend_op_array));
				hook->prop_info = copy;
				zend_persist_op_array_ex(hook, ZCG(current_persistent_script));
				copy->hooks[i] = (zend_function *)hook;
			}
		}
	}
#endif
	return copy;
}

static void zend_persist_class_constant(zval *zv, zend_class_entry *ce)
{
	zend_class_constant *c = Z_PTR_P(zv);
	zend_class_constant *copy = zend_shared_alloc_get_xlat_entry(c);
	if (copy) {
		ZVAL_PTR(zv, copy);
		return;
	}

	copy = _opkit_shared_memdup_put_md(c, sizeof(zend_class_constant));
	copy->ce = ce;
	zend_persist_zval(&copy->value);
#if PHP_VERSION_ID < 80400
	if (copy->doc_comment) {
		zend_accel_store_interned_string(copy->doc_comment);
	}
#endif
	if (copy->attributes) {
		copy->attributes = zend_persist_attributes(copy->attributes);
	}
#if PHP_VERSION_ID >= 80300
	zend_persist_type(&copy->type);
#endif
	ZVAL_PTR(zv, copy);
}

zend_class_entry *zend_persist_class_entry(zend_class_entry *orig_ce)
{
	zend_class_entry *ce = orig_ce;
	if (ce->type != ZEND_USER_CLASS) {
		return ce;
	}

	/* The same zend_class_entry may be reused by class_alias */
	zend_class_entry *new_ce = zend_shared_alloc_get_xlat_entry(ce);
	if (new_ce) {
		return new_ce;
	}

	ce = _opkit_shared_memdup_put_md(ce, sizeof(zend_class_entry));

	zend_accel_store_interned_string(ce->name);
	if (ce->parent_name) {
		zend_accel_store_interned_string(ce->parent_name);
	}

	zend_hash_persist(&ce->function_table);
	Bucket *p;
	ZEND_HASH_MAP_FOREACH_BUCKET(&ce->function_table, p) {
		if (p->key) {
			zend_accel_store_interned_string(p->key);
		}
		zend_persist_class_method(&p->val, ce);
	} ZEND_HASH_FOREACH_END();

	zend_hash_persist(&ce->properties_info);
	ZEND_HASH_MAP_FOREACH_BUCKET(&ce->properties_info, p) {
		if (p->key) {
			zend_accel_store_interned_string(p->key);
		}
		ZVAL_PTR(&p->val, zend_persist_property_info(Z_PTR(p->val)));
	} ZEND_HASH_FOREACH_END();

	if (ce->default_properties_table) {
		void *old_table = ce->default_properties_table;
		zval *copy = _opkit_shared_memdup_put_dt(ce->default_properties_table, sizeof(zval) * ce->default_properties_count);
		for (int i = 0; i < ce->default_properties_count; i++) {
			zend_persist_zval(&copy[i]);
		}
		ce->default_properties_table = copy;
		efree(old_table);
	}

	if (ce->default_static_members_table) {
		void *old_table = ce->default_static_members_table;
		zval *copy = _opkit_shared_memdup_put_dt(ce->default_static_members_table, sizeof(zval) * ce->default_static_members_count);
		for (int i = 0; i < ce->default_static_members_count; i++) {
			zend_persist_zval(&copy[i]);
		}
		ce->default_static_members_table = copy;
		efree(old_table);
	}

	zend_hash_persist(&ce->constants_table);
	ZEND_HASH_MAP_FOREACH_BUCKET(&ce->constants_table, p) {
		if (p->key) {
			zend_accel_store_interned_string(p->key);
		}
		zend_persist_class_constant(&p->val, ce);
	} ZEND_HASH_FOREACH_END();

	if (ce->num_interfaces && ce->interfaces) {
		/* Interfaces are pointers to CE. We will fix them later in zend_update_parent_ce */
		void *old_interfaces = ce->interfaces;
		ce->interfaces = _opkit_shared_memdup_put_dt(ce->interfaces, sizeof(zend_class_entry *) * ce->num_interfaces);
		efree(old_interfaces);
	}

	if (ce->num_traits && ce->trait_names) {
		void *old_trait_names = ce->trait_names;
		ce->trait_names = _opkit_shared_memdup_put_dt(ce->trait_names, sizeof(zend_class_name) * ce->num_traits);
		for (uint32_t i = 0; i < ce->num_traits; i++) {
			zend_accel_store_interned_string(ce->trait_names[i].name);
			zend_accel_store_interned_string(ce->trait_names[i].lc_name);
		}
		efree(old_trait_names);
	}

	if (ce->trait_aliases) {
		zend_trait_alias **old_aliases = ce->trait_aliases;
		int count = 0;
		while (old_aliases[count]) count++;
		zend_trait_alias **new_aliases = zend_shared_memdup_put(old_aliases, sizeof(zend_trait_alias *) * (count + 1));
		for (int i = 0; i < count; i++) {
			new_aliases[i] = zend_shared_memdup_put(old_aliases[i], sizeof(zend_trait_alias));
			if (new_aliases[i]->trait_method.method_name) {
				zend_accel_store_interned_string(new_aliases[i]->trait_method.method_name);
			}
			if (new_aliases[i]->alias) {
				zend_accel_store_interned_string(new_aliases[i]->alias);
			}
			if (new_aliases[i]->trait_method.class_name) {
				zend_accel_store_interned_string(new_aliases[i]->trait_method.class_name);
			}
			efree(old_aliases[i]);
		}
		ce->trait_aliases = new_aliases;
		efree(old_aliases);
	}

	if (ce->trait_precedences) {
		zend_trait_precedence **old_precedences = ce->trait_precedences;
		int count = 0;
		while (old_precedences[count]) count++;
		zend_trait_precedence **new_precedences = zend_shared_memdup_put(old_precedences, sizeof(zend_trait_precedence *) * (count + 1));
		for (int i = 0; i < count; i++) {
			new_precedences[i] = zend_shared_memdup_put(old_precedences[i], sizeof(zend_trait_precedence) + (old_precedences[i]->num_excludes - 1) * sizeof(zend_string *));
			if (new_precedences[i]->trait_method.method_name) {
				zend_accel_store_interned_string(new_precedences[i]->trait_method.method_name);
			}
			if (new_precedences[i]->trait_method.class_name) {
				zend_accel_store_interned_string(new_precedences[i]->trait_method.class_name);
			}
			for (uint32_t j = 0; j < new_precedences[i]->num_excludes; j++) {
				zend_accel_store_interned_string(new_precedences[i]->exclude_class_names[j]);
			}
			efree(old_precedences[i]);
		}
		ce->trait_precedences = new_precedences;
		efree(old_precedences);
	}

	if (ce->attributes) {
		ce->attributes = zend_persist_attributes(ce->attributes);
	}

#if PHP_VERSION_ID < 80400
	if (ce->info.user.doc_comment) {
		zend_accel_store_interned_string(ce->info.user.doc_comment);
	}
#endif

	return ce;
}

void zend_update_parent_ce(zend_class_entry *ce)
{
	if (ce->parent) {
		ce->parent = zend_shared_alloc_get_xlat_entry(ce->parent);
	}
	for (uint32_t i = 0; i < ce->num_interfaces; i++) {
		ce->interfaces[i] = zend_shared_alloc_get_xlat_entry(ce->interfaces[i]);
	}
}

static void zend_accel_persist_class_table(HashTable *class_table)
{
	Bucket *p;
	zend_hash_persist(class_table);
	ZEND_HASH_MAP_FOREACH_BUCKET(class_table, p) {
		ZEND_ASSERT(p->key != NULL);
		zend_accel_store_interned_string(p->key);
		ZVAL_PTR(&p->val, zend_persist_class_entry(Z_PTR(p->val)));
	} ZEND_HASH_FOREACH_END();

	ZEND_HASH_MAP_FOREACH_BUCKET(class_table, p) {
		zend_update_parent_ce(Z_PTR(p->val));
	} ZEND_HASH_FOREACH_END();
}

zend_error_info **zend_persist_warnings(uint32_t num_warnings, zend_error_info **warnings) {
	if (warnings) {
 	zend_error_info **old_warnings = warnings;
		warnings = zend_shared_memdup_put(old_warnings, num_warnings * sizeof(zend_error_info *));
		for (uint32_t i = 0; i < num_warnings; i++) {
			zend_error_info *old_info = old_warnings[i];
			warnings[i] = zend_shared_memdup_put(old_info, sizeof(zend_error_info));
			zend_accel_store_string(warnings[i]->filename);
			zend_accel_store_string(warnings[i]->message);
			efree(old_info);
		}
		efree(old_warnings);
	}
	return warnings;
}

static zend_early_binding *zend_persist_early_bindings(
		uint32_t num_early_bindings, zend_early_binding *early_bindings) {
	if (early_bindings) {
		zend_early_binding *old_bindings = early_bindings;
		early_bindings = zend_shared_memdup_put(
			old_bindings, num_early_bindings * sizeof(zend_early_binding));
		for (uint32_t i = 0; i < num_early_bindings; i++) {
			zend_accel_store_interned_string(early_bindings[i].lcname);
			zend_accel_store_interned_string(early_bindings[i].rtd_key);
			zend_accel_store_interned_string(early_bindings[i].lc_parent_name);
		}
		efree(old_bindings);
	}
	return early_bindings;
}

static void zend_accel_persist_constant_table(HashTable *constants_table)
{
	Bucket *p;
	zend_hash_persist(constants_table);
	ZEND_HASH_MAP_FOREACH_BUCKET(constants_table, p) {
		ZEND_ASSERT(p->key != NULL);
		zend_accel_store_interned_string(p->key);
		zend_constant *zc = zend_shared_memdup_put_free(Z_PTR(p->val), sizeof(zend_constant));
		ZVAL_PTR(&p->val, zc);
		if (zc->name) {
			zend_accel_store_interned_string(zc->name);
		}
		zend_persist_zval(&zc->value);
	} ZEND_HASH_FOREACH_END();
}

zend_persistent_script *zend_accel_script_persist(zend_persistent_script *script, int for_shm)
{
	Bucket *p;
	zend_persistent_script *new_script;
	opkit_persistent_script *op_script = (opkit_persistent_script *)script;

	script->mem = ZCG(mem);

	old_script_ptr = op_script;
	new_script = zend_shared_memdup_put(old_script_ptr, sizeof(opkit_persistent_script));
	opkit_persistent_script *new_op_script = (opkit_persistent_script *)new_script;

	new_script->corrupted = false;
	ZCG(current_persistent_script) = new_script;

	if (!for_shm) {
		new_script->corrupted = true;
	}

	zend_accel_store_interned_string(new_script->script.filename);

	zend_accel_persist_class_table(&new_script->script.class_table);
	zend_hash_persist(&new_script->script.function_table);
	ZEND_HASH_MAP_FOREACH_BUCKET(&new_script->script.function_table, p) {
		ZEND_ASSERT(p->key != NULL);
		zend_accel_store_interned_string(p->key);
		zend_persist_op_array(&p->val);
	} ZEND_HASH_FOREACH_END();

	zend_shared_alloc_register_xlat_entry(&old_script_ptr->script.script.main_op_array, &new_script->script.main_op_array);
	zend_persist_op_array_ex(&new_script->script.main_op_array, new_script);

	zend_accel_persist_constant_table(&new_op_script->constants_table);

	new_script->warnings = zend_persist_warnings(new_script->num_warnings, new_script->warnings);
	new_script->early_bindings = zend_persist_early_bindings(
		new_script->num_early_bindings, new_script->early_bindings);

	new_script->corrupted = false;
	ZCG(current_persistent_script) = NULL;

	new_script->size = (char*)ZCG(mem) - (char*)new_script->mem;

	return new_script;
}
