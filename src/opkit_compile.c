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
#include "php_streams.h"

#include "opkit_compile.h"
#include "opkit_module.h"
#include "opkit_wrapper.h"
#include "opkit_util_funcs.h"

#include "zend_system_id.h"
#include "zend_virtual_cwd.h"
#include "zend_attributes.h"
#include "Zend/zend_vm.h"

#include <fcntl.h>

static void *string_pool_base = NULL;
static zend_string *current_string_pool = NULL;

#define zend_accel_error(type, ...) zend_error(type, __VA_ARGS__)
#undef ACCEL_LOG_ERROR
#define ACCEL_LOG_ERROR E_ERROR
#undef ACCEL_LOG_WARNING
#define ACCEL_LOG_WARNING E_WARNING

#define SUFFIX ".phpc"

#define IS_SERIALIZED_INTERNED(ptr) \
	((size_t)(ptr) & Z_UL(1))

#define IS_SERIALIZED(ptr) \
	((char*)(ptr) <= (char*)script->size)

#define IS_UNSERIALIZED(ptr) \
	(((char*)(ptr) >= (char*)script->mem && (char*)(ptr) <= (char*)script->mem + script->size) || \
	 IS_ACCEL_INTERNED(ptr))

#define SERIALIZE_PTR(ptr) do { \
		if (ptr) { \
			(ptr) = (void*)((char*)(ptr) - (char*)script->mem); \
		} \
	} while (0)

#define UNSERIALIZE_PTR(ptr) do { \
		if (ptr) { \
			(ptr) = (void*)((char*)buf + (size_t)(ptr)); \
		} \
	} while (0)

#define UNSERIALIZED_PTR(ptr) \
	(((ptr) && (char*)(ptr) <= (char*)script->size) ? (void*)((char*)buf + (size_t)(ptr)) : (void*)(ptr))

#define SERIALIZE_STR(ptr) do { \
		if (ptr && !IS_SERIALIZED(ptr)) { \
			if (IS_ACCEL_INTERNED(ptr)) { \
				(ptr) = zend_file_cache_serialize_interned((zend_string*)(ptr), info); \
			} else { \
				(ptr) = (void*)((char*)(ptr) - (char*)script->mem); \
			} \
		} \
	} while (0)

#define UNSERIALIZE_STR(ptr) do { \
		if (ptr && IS_SERIALIZED(ptr)) { \
			if (IS_SERIALIZED_INTERNED(ptr)) { \
				(ptr) = (void*)zend_file_cache_unserialize_interned((zend_string*)(ptr)); \
			} else { \
				(ptr) = (void*)((char*)buf + (size_t)(ptr)); \
			} \
		} \
	} while (0)

#define SERIALIZE_ATTRIBUTES(attributes) do { \
	if (attributes) { \
		HashTable *ht; \
		SERIALIZE_PTR(attributes); \
		ht = (attributes); \
		ht = UNSERIALIZED_PTR(ht); \
		zend_file_cache_serialize_hash(ht, script, info, buf, zend_file_cache_serialize_attribute); \
	} \
} while (0)

#define UNSERIALIZE_ATTRIBUTES(ptr) do { \
		if (ptr) { \
			HashTable *ht; \
			UNSERIALIZE_PTR(ptr); \
			ht = (ptr); \
			zend_file_cache_unserialize_hash(ht, script, buf, zend_file_cache_unserialize_attribute, NULL); \
		} \
	} while (0)

typedef void (*serialize_callback_t)(zval *, zend_persistent_script *, zend_file_cache_metainfo *, void *);

static void zend_file_cache_serialize_zval(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf);
static void zend_file_cache_serialize_hash(HashTable *ht, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf, serialize_callback_t func);
static void zend_file_cache_serialize_attribute(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf);

static void *zend_file_cache_serialize_interned(zend_string *str, zend_file_cache_metainfo *info)
{
	size_t len;
	void *ret;

	ret = zend_shared_alloc_get_xlat_entry(str);
	if (ret) {
		return ret;
	}

	len = _ZSTR_STRUCT_SIZE(ZSTR_LEN(str));
	ret = (void*)(info->str_size | Z_UL(1));
	zend_shared_alloc_register_xlat_entry(str, ret);

	if (info->str_size + len > ZSTR_LEN(current_string_pool)) {
		size_t new_len = info->str_size + len;
		current_string_pool = zend_string_realloc(
			current_string_pool,
			((_ZSTR_HEADER_SIZE + 1 + new_len + 4095) & ~0xfff) - (_ZSTR_HEADER_SIZE + 1),
			0);
		string_pool_base = (void*)ZSTR_VAL(current_string_pool);
	}

	zend_string *new_str = (zend_string *) (ZSTR_VAL(current_string_pool) + info->str_size);
	memcpy(new_str, str, len);
	info->str_size += ZEND_ALIGNED_SIZE(len);
	return ret;
}

static void *zend_file_cache_unserialize_interned(zend_string *str)
{
	zend_string *res = (zend_string*)((char*)string_pool_base + ((size_t)(str) & ~Z_UL(1)));
	return zend_new_interned_string(zend_string_copy(res));
}

static void zend_file_cache_serialize_hash(HashTable *ht, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf, serialize_callback_t func)
{
	if (!ht || (HT_FLAGS(ht) & HASH_FLAG_UNINITIALIZED) || !ht->arData) {
		if (ht) ht->arData = NULL;
		return;
	}
	if (IS_SERIALIZED(ht->arData)) {
		return;
	}
	if (HT_IS_PACKED(ht)) {
		zval *p, *end;

		SERIALIZE_PTR(ht->arPacked);
		p = ht->arPacked;
		p = UNSERIALIZED_PTR(p);
		if (p) {
			end = p + ht->nNumUsed;
			while (p < end) {
				if (Z_TYPE_P(p) != IS_UNDEF) {
					func(p, script, info, buf);
				}
				p++;
			}
		}
	} else {
		Bucket *p, *end;

		SERIALIZE_PTR(ht->arData);
		p = ht->arData;
		p = UNSERIALIZED_PTR(p);
		if (p) {
			end = p + ht->nNumUsed;
			while (p < end) {
				if (Z_TYPE(p->val) != IS_UNDEF) {
					if (p->key) {
						SERIALIZE_STR(p->key);
					}
					func(&p->val, script, info, buf);
				}
				p++;
			}
		}
	}
}

static void zend_file_cache_serialize_ast(zend_ast *ast, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	uint32_t i;

	if (ast->kind == ZEND_AST_ZVAL || ast->kind == ZEND_AST_CONSTANT) {
		zend_file_cache_serialize_zval(&((zend_ast_zval*)ast)->val, script, info, buf);
	} else if (zend_ast_is_list(ast)) {
		zend_ast_list *list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			if (list->child[i]) {
				SERIALIZE_PTR(list->child[i]);
				zend_file_cache_serialize_ast(UNSERIALIZED_PTR(list->child[i]), script, info, buf);
			}
		}
	} else {
		uint32_t children = zend_ast_get_num_children(ast);
		for (i = 0; i < children; i++) {
			if (ast->child[i]) {
				SERIALIZE_PTR(ast->child[i]);
				zend_file_cache_serialize_ast(UNSERIALIZED_PTR(ast->child[i]), script, info, buf);
			}
		}
	}
}

static void zend_file_cache_serialize_zval(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	switch (Z_TYPE_P(zv)) {
		case IS_STRING:
			SERIALIZE_STR(Z_STR_P(zv));
			break;
		case IS_ARRAY:
			if (!IS_SERIALIZED(Z_ARRVAL_P(zv))) {
				HashTable *ht;
				SERIALIZE_PTR(Z_ARR_P(zv));
				ht = Z_ARRVAL_P(zv);
				ht = UNSERIALIZED_PTR(ht);
				zend_file_cache_serialize_hash(ht, script, info, buf, zend_file_cache_serialize_zval);
			}
			break;
		case IS_CONSTANT_AST:
			if (!IS_SERIALIZED(Z_AST_P(zv))) {
				zend_ast_ref *ast_ref;
				SERIALIZE_PTR(Z_AST_P(zv));
				ast_ref = Z_AST_P(zv);
				ast_ref = UNSERIALIZED_PTR(ast_ref);
				zend_file_cache_serialize_ast(GC_AST(ast_ref), script, info, buf);
			}
			break;
	}
}

static void zend_file_cache_serialize_attribute(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	zend_attribute *attr;
	uint32_t i;

	SERIALIZE_PTR(Z_PTR_P(zv));
	attr = Z_PTR_P(zv);
	UNSERIALIZE_PTR(attr);

	SERIALIZE_STR(attr->name);
	SERIALIZE_STR(attr->lcname);
	for (i = 0; i < attr->argc; i++) {
		SERIALIZE_STR(attr->args[i].name);
		zend_file_cache_serialize_zval(&attr->args[i].value, script, info, buf);
	}
}

static void zend_file_cache_serialize_type(zend_type *type, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	if (ZEND_TYPE_HAS_LIST(*type)) {
		zend_type_list *list = ZEND_TYPE_LIST(*type);
		if (!IS_SERIALIZED(list)) {
			uint32_t i;
			SERIALIZE_PTR(list);
			UNSERIALIZE_PTR(list);
			for (i = 0; i < list->num_types; i++) {
				zend_file_cache_serialize_type(&list->types[i], script, info, buf);
			}
		}
	} else if (ZEND_TYPE_HAS_NAME(*type)) {
		zend_string *type_name = ZEND_TYPE_NAME(*type);
		SERIALIZE_STR(type_name);
	}
}

static void zend_file_cache_serialize_op_array(zend_op_array *op_array, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	if (op_array->static_variables) {
		HashTable *ht;
		SERIALIZE_PTR(op_array->static_variables);
		ht = op_array->static_variables;
		ht = UNSERIALIZED_PTR(ht);
		zend_file_cache_serialize_hash(ht, script, info, buf, zend_file_cache_serialize_zval);
	}
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
	SERIALIZE_PTR(op_array->opcodes);
	if (op_array->opcodes) {
		zend_op *opline = op_array->opcodes;
		opline = UNSERIALIZED_PTR(opline);
		if (opline) {
			zend_op *end = opline + op_array->last;
			while (opline < end) {
#if ZEND_USE_ABS_CONST_ADDR
				if (opline->op1_type == IS_CONST) {
					SERIALIZE_PTR(opline->op1.zv);
				}
				if (opline->op2_type == IS_CONST) {
					SERIALIZE_PTR(opline->op2.zv);
				}
#endif
#if ZEND_USE_ABS_JMP_ADDR
				switch (opline->opcode) {
					case ZEND_JMP:
					case ZEND_FAST_CALL:
						SERIALIZE_PTR(opline->op1.jmp_addr);
						break;
					case ZEND_JMPZ:
					case ZEND_JMPNZ:
					case ZEND_JMPZ_EX:
					case ZEND_JMPNZ_EX:
					case ZEND_JMP_SET:
					case ZEND_COALESCE:
					case ZEND_FE_RESET_R:
					case ZEND_FE_RESET_RW:
					case ZEND_ASSERT_CHECK:
					case ZEND_JMP_NULL:
					case ZEND_BIND_INIT_STATIC_OR_JMP:
						SERIALIZE_PTR(opline->op2.jmp_addr);
						break;
					case ZEND_CATCH:
						if (!(opline->extended_value & ZEND_LAST_CATCH)) {
							SERIALIZE_PTR(opline->op2.jmp_addr);
						}
						break;
				}
#endif
				zend_serialize_opcode_handler(opline);
				opline++;
			}
		}
	}
	if (op_array->arg_info) {
		zend_arg_info *arg_info;
		uint32_t num_args, i;
		SERIALIZE_PTR(op_array->arg_info);
		arg_info = op_array->arg_info;
		if (arg_info) {
			arg_info = UNSERIALIZED_PTR(arg_info);
			num_args = op_array->num_args;
			if (op_array->fn_flags & ZEND_ACC_VARIADIC) num_args++;
			if (op_array->fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
				arg_info--;
				num_args++;
			}
			for (i = 0; i < num_args; i++) {
				SERIALIZE_STR(arg_info[i].name);
				zend_file_cache_serialize_type(&arg_info[i].type, script, info, buf);
			}
		}
	}
	SERIALIZE_STR(op_array->function_name);
	SERIALIZE_STR(op_array->filename);
#if PHP_VERSION_ID < 80400
	SERIALIZE_STR(op_array->doc_comment);
#endif
	if (op_array->vars) {
		zend_string **vars;
		uint32_t i;
		SERIALIZE_PTR(op_array->vars);
		vars = op_array->vars;
		if (vars) {
			vars = UNSERIALIZED_PTR(vars);
			for (i = 0; i < op_array->last_var; i++) {
				SERIALIZE_STR(vars[i]);
			}
		}
	}
	SERIALIZE_PTR(op_array->live_range);
	SERIALIZE_PTR(op_array->scope);
	SERIALIZE_PTR(op_array->try_catch_array);
	SERIALIZE_ATTRIBUTES(op_array->attributes);

	if (op_array->num_dynamic_func_defs) {
		zend_op_array **defs;
		uint32_t i;
		SERIALIZE_PTR(op_array->dynamic_func_defs);
		defs = op_array->dynamic_func_defs;
		if (defs) {
			defs = UNSERIALIZED_PTR(defs);
			for (i = 0; i < op_array->num_dynamic_func_defs; i++) {
				zend_op_array *def;
				SERIALIZE_PTR(defs[i]);
				def = defs[i];
				def = UNSERIALIZED_PTR(def);
				if (def) {
					zend_file_cache_serialize_op_array(def, script, info, buf);
				}
			}
		}
	}
}

static void zend_file_cache_serialize_func(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	zend_op_array *op_array;
	if (IS_SERIALIZED(Z_PTR_P(zv))) {
		return;
	}
	SERIALIZE_PTR(Z_PTR_P(zv));
	op_array = Z_PTR_P(zv);
	op_array = UNSERIALIZED_PTR(op_array);
	if (!op_array) return;
	if (op_array->type == ZEND_USER_FUNCTION) {
		zend_file_cache_serialize_op_array(op_array, script, info, buf);
	}
}

static void zend_file_cache_serialize_class_constant(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	zend_class_constant *c;
	SERIALIZE_PTR(Z_PTR_P(zv));
	c = Z_PTR_P(zv);
	c = UNSERIALIZED_PTR(c);
	if (c) {
		zend_file_cache_serialize_zval(&c->value, script, info, buf);
		if (c->doc_comment) {
			SERIALIZE_STR(c->doc_comment);
		}
		SERIALIZE_ATTRIBUTES(c->attributes);
#if PHP_VERSION_ID >= 80300
		zend_file_cache_serialize_type(&c->type, script, info, buf);
#endif
		SERIALIZE_PTR(c->ce);
	}
}

static void zend_file_cache_serialize_prop_info(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	if (!IS_SERIALIZED(Z_PTR_P(zv))) {
		zend_property_info *prop;
		SERIALIZE_PTR(Z_PTR_P(zv));
		prop = Z_PTR_P(zv);
		prop = UNSERIALIZED_PTR(prop);

		if (prop && !IS_SERIALIZED(prop->ce)) {
			SERIALIZE_PTR(prop->ce);
			SERIALIZE_STR(prop->name);
			if (prop->doc_comment) {
				SERIALIZE_STR(prop->doc_comment);
			}
			SERIALIZE_ATTRIBUTES(prop->attributes);
#if PHP_VERSION_ID >= 80400
			SERIALIZE_PTR(prop->prototype);
			/* Serialize property hooks */
			if (prop->hooks) {
				zend_function **hooks = prop->hooks;
				SERIALIZE_PTR(prop->hooks);
				hooks = UNSERIALIZED_PTR(hooks);
				for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
					if (hooks[i]) {
						SERIALIZE_PTR(hooks[i]);
						zend_function *hook = hooks[i];
						hook = UNSERIALIZED_PTR(hook);
						zend_file_cache_serialize_op_array(&hook->op_array, script, info, buf);
					}
				}
			}
#endif
			zend_file_cache_serialize_type(&prop->type, script, info, buf);
		}
	}
}

static void zend_file_cache_serialize_class(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	zend_class_entry *ce;

	SERIALIZE_PTR(Z_PTR_P(zv));
	ce = Z_PTR_P(zv);
	ce = UNSERIALIZED_PTR(ce);

	SERIALIZE_STR(ce->name);
	if (ce->parent) {
		if (!(ce->ce_flags & ZEND_ACC_LINKED)) {
			SERIALIZE_STR(ce->parent_name);
		} else {
			SERIALIZE_PTR(ce->parent);
		}
	}
	zend_file_cache_serialize_hash(&ce->function_table, script, info, buf, zend_file_cache_serialize_func);

	if (ce->default_properties_table) {
		zval *p, *end;
		SERIALIZE_PTR(ce->default_properties_table);
		p = ce->default_properties_table;
		p = UNSERIALIZED_PTR(p);
		end = p + ce->default_properties_count;
		while (p < end) {
			zend_file_cache_serialize_zval(p, script, info, buf);
			p++;
		}
	}
	if (ce->default_static_members_table) {
		zval *p, *end;
		SERIALIZE_PTR(ce->default_static_members_table);
		p = ce->default_static_members_table;
		p = UNSERIALIZED_PTR(p);
		end = p + ce->default_static_members_count;
		while (p < end) {
			zend_file_cache_serialize_zval(p, script, info, buf);
			p++;
		}
	}
	zend_file_cache_serialize_hash(&ce->constants_table, script, info, buf, zend_file_cache_serialize_class_constant);
	zend_file_cache_serialize_hash(&ce->properties_info, script, info, buf, zend_file_cache_serialize_prop_info);

	if (ce->properties_info_table) {
		uint32_t i;
		zend_property_info **table;
		SERIALIZE_PTR(ce->properties_info_table);
		table = ce->properties_info_table;
		table = UNSERIALIZED_PTR(table);
		for (i = 0; i < ce->default_properties_count; i++) {
			SERIALIZE_PTR(table[i]);
		}
	}

	/* Serialize magic methods */
	SERIALIZE_PTR(ce->constructor);
	SERIALIZE_PTR(ce->destructor);
	SERIALIZE_PTR(ce->clone);
	SERIALIZE_PTR(ce->__get);
	SERIALIZE_PTR(ce->__set);
	SERIALIZE_PTR(ce->__unset);
	SERIALIZE_PTR(ce->__isset);
	SERIALIZE_PTR(ce->__call);
	SERIALIZE_PTR(ce->__callstatic);
	SERIALIZE_PTR(ce->__tostring);
	SERIALIZE_PTR(ce->__debugInfo);
	SERIALIZE_PTR(ce->__serialize);
	SERIALIZE_PTR(ce->__unserialize);

	ZEND_MAP_PTR_INIT(ce->static_members_table, NULL);
	ZEND_MAP_PTR_INIT(ce->mutable_data, NULL);

	ce->inheritance_cache = NULL;

	if (ce->type == ZEND_USER_CLASS) {
		SERIALIZE_STR(ce->info.user.filename);
#if PHP_VERSION_ID >= 80400
		SERIALIZE_STR(ce->doc_comment);
#else
		SERIALIZE_STR(ce->info.user.doc_comment);
#endif
	}
	SERIALIZE_ATTRIBUTES(ce->attributes);
}

static void zend_file_cache_serialize_constant(zval *zv, zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	zend_constant *zc;
	SERIALIZE_PTR(Z_PTR_P(zv));
	zc = Z_PTR_P(zv);
	zc = UNSERIALIZED_PTR(zc);
	if (zc) {
		if (zc->name) {
			SERIALIZE_STR(zc->name);
		}
		zend_file_cache_serialize_zval(&zc->value, script, info, buf);
	}
}

static void zend_file_cache_serialize_warnings(zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	if (script->warnings) {
		zend_error_info **warnings;
		uint32_t i;
		SERIALIZE_PTR(script->warnings);
		warnings = script->warnings;
		if (warnings) {
			warnings = UNSERIALIZED_PTR(warnings);
		}
		for (i = 0; i < script->num_warnings; i++) {
			zend_error_info *error;
			SERIALIZE_PTR(warnings[i]);
			error = warnings[i];
			if (error) {
				error = UNSERIALIZED_PTR(error);
				SERIALIZE_STR(error->filename);
				SERIALIZE_STR(error->message);
			}
		}
	}
}

static void zend_file_cache_serialize_early_bindings(zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	if (script->early_bindings) {
		zend_early_binding *early_bindings;
		uint32_t i;
		SERIALIZE_PTR(script->early_bindings);
		early_bindings = script->early_bindings;
		if (early_bindings) {
			early_bindings = UNSERIALIZED_PTR(early_bindings);
		}
		for (i = 0; i < script->num_early_bindings; i++) {
			SERIALIZE_STR(early_bindings[i].lcname);
			SERIALIZE_STR(early_bindings[i].rtd_key);
			SERIALIZE_STR(early_bindings[i].lc_parent_name);
		}
	}
}

static void zend_file_cache_serialize(zend_persistent_script *script, zend_file_cache_metainfo *info, void *buf)
{
	SERIALIZE_STR(script->script.filename);
	zend_file_cache_serialize_hash(&script->script.class_table, script, info, buf, zend_file_cache_serialize_class);
	zend_file_cache_serialize_hash(&script->script.function_table, script, info, buf, zend_file_cache_serialize_func);
	zend_file_cache_serialize_op_array(&script->script.main_op_array, script, info, buf);
	zend_file_cache_serialize_hash(&((opkit_persistent_script*)script)->constants_table, script, info, buf, zend_file_cache_serialize_constant);
	zend_file_cache_serialize_warnings(script, info, buf);
	zend_file_cache_serialize_early_bindings(script, info, buf);
}

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_UIO_H
# include <sys/uio.h>
#endif

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif

#define opkit_compile_unlink unlink
#define opkit_compile_open open

#if defined(HAVE_FLOCK)
# define opkit_compile_flock flock
#endif

#ifndef O_BINARY
#  define O_BINARY 0
#endif

static void zend_file_cache_unserialize_zval(zval *zv, zend_persistent_script *script, void *buf);
static void zend_file_cache_unserialize_hash(HashTable *ht, zend_persistent_script *script, void *buf, void (*unserialize_func)(zval *, zend_persistent_script *, void *), dtor_func_t dtor);
static void zend_file_cache_unserialize_ast(zend_ast *ast, zend_persistent_script *script, void *buf);
static void zend_file_cache_unserialize_func(zval *zv, zend_persistent_script *script, void *buf);

static void zend_file_cache_unserialize_ast(zend_ast *ast, zend_persistent_script *script, void *buf)
{
	uint32_t i;

	if (ast->kind == ZEND_AST_ZVAL || ast->kind == ZEND_AST_CONSTANT) {
		zend_file_cache_unserialize_zval(&((zend_ast_zval*)ast)->val, script, buf);
	} else if (zend_ast_is_list(ast)) {
		zend_ast_list *list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			if (list->child[i] && !IS_UNSERIALIZED(list->child[i])) {
				UNSERIALIZE_PTR(list->child[i]);
				zend_file_cache_unserialize_ast(list->child[i], script, buf);
			}
		}
	} else {
		uint32_t children = zend_ast_get_num_children(ast);
		for (i = 0; i < children; i++) {
			if (ast->child[i] && !IS_UNSERIALIZED(ast->child[i])) {
				UNSERIALIZE_PTR(ast->child[i]);
				zend_file_cache_unserialize_ast(ast->child[i], script, buf);
			}
		}
	}
}

static void zend_file_cache_unserialize_hash(HashTable *ht, zend_persistent_script *script, void *buf, void (*unserialize_func)(zval *, zend_persistent_script *, void *), dtor_func_t dtor)
{
	ht->pDestructor = dtor;
	if (HT_FLAGS(ht) & HASH_FLAG_UNINITIALIZED) {
		ht->arData = (Bucket *)&uninitialized_bucket;
		return;
	}
	if (!IS_SERIALIZED(ht->arData)) {
		return;
	}

	UNSERIALIZE_PTR(ht->arData);

	if (HT_IS_PACKED(ht)) {
		zval *p, *end;

		p = ht->arPacked;
		if (p) {
			end = p + ht->nNumUsed;
			while (p < end) {
				if (Z_TYPE_P(p) != IS_UNDEF) {
					unserialize_func(p, script, buf);
				}
				p++;
			}
		}
	} else {
		Bucket *p, *end;

		p = ht->arData;
		end = p + ht->nNumUsed;
		while (p < end) {
			if (Z_TYPE(p->val) != IS_UNDEF) {
				UNSERIALIZE_STR(p->key);
				unserialize_func(&p->val, script, buf);
			}
			p++;
		}
	}
}

static void zend_file_cache_unserialize_zval(zval *zv, zend_persistent_script *script, void *buf)
{
	switch (Z_TYPE_P(zv)) {
		case IS_STRING:
			if (IS_SERIALIZED(Z_STR_P(zv)) || IS_SERIALIZED_INTERNED(Z_STR_P(zv))) {
				UNSERIALIZE_STR(Z_STR_P(zv));
			}
			break;
		case IS_ARRAY:
			if (!IS_UNSERIALIZED(Z_ARR_P(zv))) {
				HashTable *ht;
				UNSERIALIZE_PTR(Z_ARR_P(zv));
				ht = Z_ARR_P(zv);
				zend_file_cache_unserialize_hash(ht, script, buf, zend_file_cache_unserialize_zval, ZVAL_PTR_DTOR);
			}
			break;
		case IS_CONSTANT_AST:
			if (!IS_UNSERIALIZED(Z_AST_P(zv))) {
				zend_ast_ref *ast_ref;
				UNSERIALIZE_PTR(Z_AST_P(zv));
				ast_ref = Z_AST_P(zv);
				zend_file_cache_unserialize_ast(GC_AST(ast_ref), script, buf);
			}
			break;
		case IS_INDIRECT:
			UNSERIALIZE_PTR(Z_INDIRECT_P(zv));
			break;
	}
}

static void zend_file_cache_unserialize_attribute(zval *zv, zend_persistent_script *script, void *buf)
{
	zend_attribute *attr;
	uint32_t i;

	if (!IS_UNSERIALIZED(Z_PTR_P(zv))) {
		UNSERIALIZE_PTR(Z_PTR_P(zv));
		attr = Z_PTR_P(zv);
		UNSERIALIZE_STR(attr->name);
		UNSERIALIZE_STR(attr->lcname);
		for (i = 0; i < attr->argc; i++) {
			UNSERIALIZE_STR(attr->args[i].name);
			zend_file_cache_unserialize_zval(&attr->args[i].value, script, buf);
		}
	}
}

static void zend_file_cache_unserialize_type(zend_type *type, zend_class_entry *scope, zend_persistent_script *script, void *buf)
{
	if (ZEND_TYPE_HAS_LIST(*type)) {
		zend_type_list *list = ZEND_TYPE_LIST(*type);
		uint32_t i;
		UNSERIALIZE_PTR(list);
		ZEND_TYPE_SET_PTR(*type, list);
		for (i = 0; i < list->num_types; i++) {
			zend_file_cache_unserialize_type(&list->types[i], scope, script, buf);
		}
	} else if (ZEND_TYPE_HAS_NAME(*type)) {
		zend_string *type_name = ZEND_TYPE_NAME(*type);
		UNSERIALIZE_STR(type_name);
		ZEND_TYPE_SET_PTR(*type, type_name);
		if (!script->corrupted) {
			zend_accel_get_class_name_map_ptr(type_name);
		} else {
			zend_alloc_ce_cache(type_name);
		}
	}
}

static void zend_file_cache_unserialize_op_array(zend_op_array *op_array, zend_persistent_script *script, void *buf)
{
	op_array->fn_flags |= ZEND_ACC_DONE_PASS_TWO;
	op_array->fn_flags &= ~ZEND_ACC_IMMUTABLE;
	op_array->fn_flags &= ~ZEND_ACC_ARENA_ALLOCATED;

	if (IS_UNSERIALIZED(op_array->opcodes)) {
		return;
	}
	ZEND_MAP_PTR_INIT(op_array->static_variables_ptr, NULL);
	if (op_array->cache_size > 0) {
		void *ptr = (void*)((char*)script->mem + (uintptr_t)op_array->run_time_cache__ptr);
		op_array->fn_flags |= ZEND_ACC_HEAP_RT_CACHE;
		ZEND_MAP_PTR_INIT(op_array->run_time_cache, ptr);
		memset(ptr, 0, op_array->cache_size);
	} else {
		ZEND_MAP_PTR_INIT(op_array->run_time_cache, NULL);
	}

	if (op_array->static_variables) {
		UNSERIALIZE_PTR(op_array->static_variables);
		zend_file_cache_unserialize_hash(op_array->static_variables, script, buf, zend_file_cache_unserialize_zval, ZVAL_PTR_DTOR);
	}
	if (op_array->literals) {
		zval *p, *end;
		UNSERIALIZE_PTR(op_array->literals);
		p = op_array->literals;
		end = p + op_array->last_literal;
		while (p < end) {
			zend_file_cache_unserialize_zval(p, script, buf);
			p++;
		}
	}
	UNSERIALIZE_PTR(op_array->opcodes);
	if (op_array->opcodes) {
		zend_op *opline = op_array->opcodes;
		zend_op *end = opline + op_array->last;
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
				case ZEND_JMPZ_EX:
				case ZEND_JMPNZ_EX:
				case ZEND_JMP_SET:
				case ZEND_COALESCE:
				case ZEND_FE_RESET_R:
				case ZEND_FE_RESET_RW:
				case ZEND_ASSERT_CHECK:
				case ZEND_JMP_NULL:
				case ZEND_BIND_INIT_STATIC_OR_JMP:
					UNSERIALIZE_PTR(opline->op2.jmp_addr);
					break;
				case ZEND_CATCH:
					if (!(opline->extended_value & ZEND_LAST_CATCH)) {
						UNSERIALIZE_PTR(opline->op2.jmp_addr);
					}
					break;
			}
#endif
			zend_deserialize_opcode_handler(opline);
			opline++;
		}
	}
	if (op_array->arg_info) {
		zend_arg_info *arg_info;
		uint32_t num_args, i;
		UNSERIALIZE_PTR(op_array->arg_info);
		arg_info = op_array->arg_info;
		num_args = op_array->num_args;
		if (op_array->fn_flags & ZEND_ACC_VARIADIC) num_args++;
		if (op_array->fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
			arg_info--;
			num_args++;
		}
		for (i = 0; i < num_args; i++) {
			UNSERIALIZE_STR(arg_info[i].name);
			zend_file_cache_unserialize_type(&arg_info[i].type, op_array->scope, script, buf);
		}
	}
	UNSERIALIZE_STR(op_array->function_name);
	UNSERIALIZE_STR(op_array->filename);
#if PHP_VERSION_ID < 80400
	UNSERIALIZE_STR(op_array->doc_comment);
#endif
	UNSERIALIZE_PTR(op_array->vars);
	if (op_array->vars) {
		for (int i = 0; i < op_array->last_var; i++) {
			UNSERIALIZE_STR(op_array->vars[i]);
		}
	}
	UNSERIALIZE_PTR(op_array->live_range);
	UNSERIALIZE_PTR(op_array->scope);
	UNSERIALIZE_PTR(op_array->try_catch_array);
	UNSERIALIZE_ATTRIBUTES(op_array->attributes);

	if (op_array->num_dynamic_func_defs) {
		UNSERIALIZE_PTR(op_array->dynamic_func_defs);
		for (uint32_t i = 0; i < op_array->num_dynamic_func_defs; i++) {
			UNSERIALIZE_PTR(op_array->dynamic_func_defs[i]);
			zend_file_cache_unserialize_op_array(op_array->dynamic_func_defs[i], script, buf);
		}
	}
}

static void zend_file_cache_unserialize_class_constant(zval *zv, zend_persistent_script *script, void *buf)
{
	if (!IS_UNSERIALIZED(Z_PTR_P(zv))) {
		zend_class_constant *c;
		UNSERIALIZE_PTR(Z_PTR_P(zv));
		c = Z_PTR_P(zv);
		if (c) {
			if (!IS_UNSERIALIZED(c->ce)) {
				UNSERIALIZE_PTR(c->ce);
				zend_file_cache_unserialize_zval(&c->value, script, buf);
				if (c->doc_comment) {
					UNSERIALIZE_STR(c->doc_comment);
				}
				UNSERIALIZE_ATTRIBUTES(c->attributes);
#if PHP_VERSION_ID >= 80300
				zend_file_cache_unserialize_type(&c->type, c->ce, script, buf);
#endif
			}
		}
	}
}

static void zend_file_cache_unserialize_prop_info(zval *zv, zend_persistent_script *script, void *buf)
{
	if (!IS_UNSERIALIZED(Z_PTR_P(zv))) {
		zend_property_info *prop;
		UNSERIALIZE_PTR(Z_PTR_P(zv));
		prop = Z_PTR_P(zv);
		if (prop) {
			/* PHP 8.4 uses prop->ce being serialized as indicator that prop needs unserialization */
			if (!IS_UNSERIALIZED(prop->ce)) {
				UNSERIALIZE_PTR(prop->ce);
				UNSERIALIZE_STR(prop->name);
				if (prop->doc_comment) {
					UNSERIALIZE_STR(prop->doc_comment);
				}
				UNSERIALIZE_ATTRIBUTES(prop->attributes);
#if PHP_VERSION_ID >= 80400
				UNSERIALIZE_PTR(prop->prototype);
				if (prop->hooks) {
					UNSERIALIZE_PTR(prop->hooks);
					for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
						if (prop->hooks[i]) {
							UNSERIALIZE_PTR(prop->hooks[i]);
							zend_file_cache_unserialize_op_array(&prop->hooks[i]->op_array, script, buf);
						}
					}
				}
#endif
				zend_file_cache_unserialize_type(&prop->type, prop->ce, script, buf);
			}
		}
	}
}

uint32_t zend_accel_get_class_name_map_ptr(zend_string *type_name)
{
	uint32_t ret;

	if (zend_string_equals_literal_ci(type_name, "self") ||
			zend_string_equals_literal_ci(type_name, "parent")) {
		return 0;
			}

	/* We use type.name.gc.refcount to keep map_ptr of corresponding type */
	if (ZSTR_HAS_CE_CACHE(type_name)) {
		return GC_REFCOUNT(type_name);
	}

	if ((GC_FLAGS(type_name) & GC_IMMUTABLE)
	 && (GC_FLAGS(type_name) & IS_STR_PERMANENT)) {
		do {
			ret = ZEND_MAP_PTR_NEW_OFFSET();
		} while (ret <= 2);
		GC_SET_REFCOUNT(type_name, ret);
		GC_ADD_FLAGS(type_name, IS_STR_CLASS_NAME_MAP_PTR);
		return ret;
	 }

	return 0;
}

static void zend_file_cache_unserialize_class(zval *zv, zend_persistent_script *script, void *buf)
{
	zend_class_entry *ce;
	UNSERIALIZE_PTR(Z_PTR_P(zv));
	ce = Z_PTR_P(zv);

	UNSERIALIZE_STR(ce->name);

	if (!(ce->ce_flags & ZEND_ACC_ANON_CLASS)) {
		if (!script->corrupted) {
			zend_accel_get_class_name_map_ptr(ce->name);
		} else {
			zend_alloc_ce_cache(ce->name);
		}
	}

	if (ce->parent) {
		if (!(ce->ce_flags & ZEND_ACC_LINKED)) {
			UNSERIALIZE_STR(ce->parent_name);
		} else {
			UNSERIALIZE_PTR(ce->parent);
		}
	}
	/*
	UNSERIALIZE_STR(ce->parent_name);
	*/
	ce->ce_flags |= ZEND_ACC_IMMUTABLE;
	zend_file_cache_unserialize_hash(&ce->function_table, script, buf, zend_file_cache_unserialize_func, NULL);

	if (ce->default_properties_table) {
		UNSERIALIZE_PTR(ce->default_properties_table);
		for (int i = 0; i < ce->default_properties_count; i++) {
			zend_file_cache_unserialize_zval(&ce->default_properties_table[i], script, buf);
		}
	}

	if (ce->default_static_members_table) {
		UNSERIALIZE_PTR(ce->default_static_members_table);
		for (int i = 0; i < ce->default_static_members_count; i++) {
			zend_file_cache_unserialize_zval(&ce->default_static_members_table[i], script, buf);
		}
	}
	zend_file_cache_unserialize_hash(&ce->constants_table, script, buf, zend_file_cache_unserialize_class_constant, NULL);
	zend_file_cache_unserialize_hash(&ce->properties_info, script, buf, zend_file_cache_unserialize_prop_info, NULL);

	if (ce->properties_info_table) {
		uint32_t i;
		UNSERIALIZE_PTR(ce->properties_info_table);
		for (i = 0; i < ce->default_properties_count; i++) {
			UNSERIALIZE_PTR(ce->properties_info_table[i]);
		}
	}

	/* Unserialize magic methods */
	UNSERIALIZE_PTR(ce->constructor);
	UNSERIALIZE_PTR(ce->destructor);
	UNSERIALIZE_PTR(ce->clone);
	UNSERIALIZE_PTR(ce->__get);
	UNSERIALIZE_PTR(ce->__set);
	UNSERIALIZE_PTR(ce->__unset);
	UNSERIALIZE_PTR(ce->__isset);
	UNSERIALIZE_PTR(ce->__call);
	UNSERIALIZE_PTR(ce->__callstatic);
	UNSERIALIZE_PTR(ce->__tostring);
	UNSERIALIZE_PTR(ce->__debugInfo);
	UNSERIALIZE_PTR(ce->__serialize);
	UNSERIALIZE_PTR(ce->__unserialize);

	if (ce->type == ZEND_USER_CLASS) {
		UNSERIALIZE_STR(ce->info.user.filename);
#if PHP_VERSION_ID >= 80400
		UNSERIALIZE_STR(ce->doc_comment);
#else
		UNSERIALIZE_STR(ce->info.user.doc_comment);
#endif
	}
	UNSERIALIZE_ATTRIBUTES(ce->attributes);
}

static void zend_file_cache_unserialize_func(zval *zv, zend_persistent_script *script, void *buf)
{
	zend_op_array *op_array;
	UNSERIALIZE_PTR(Z_PTR_P(zv));
	op_array = Z_PTR_P(zv);
	if (op_array && op_array->type == ZEND_USER_FUNCTION) {
		zend_file_cache_unserialize_op_array(op_array, script, buf);
	}
}

static void zend_file_cache_unserialize_constant(zval *zv, zend_persistent_script *script, void *buf)
{
	zend_constant *zc;
	UNSERIALIZE_PTR(Z_PTR_P(zv));
	zc = Z_PTR_P(zv);
	if (zc) {
		UNSERIALIZE_STR(zc->name);
		zend_file_cache_unserialize_zval(&zc->value, script, buf);
	}
}

static void zend_file_cache_unserialize_warnings(zend_persistent_script *script, void *buf)
{
	if (script->warnings) {
		UNSERIALIZE_PTR(script->warnings);
		for (uint32_t i = 0; i < script->num_warnings; i++) {
			UNSERIALIZE_PTR(script->warnings[i]);
			UNSERIALIZE_STR(script->warnings[i]->filename);
			UNSERIALIZE_STR(script->warnings[i]->message);
		}
	}
}

static void zend_file_cache_unserialize_early_bindings(zend_persistent_script *script, void *buf)
{
	if (script->early_bindings) {
		UNSERIALIZE_PTR(script->early_bindings);
		for (uint32_t i = 0; i < script->num_early_bindings; i++) {
			UNSERIALIZE_STR(script->early_bindings[i].lcname);
			UNSERIALIZE_STR(script->early_bindings[i].rtd_key);
			UNSERIALIZE_STR(script->early_bindings[i].lc_parent_name);
		}
	}
}

static void zend_file_cache_unserialize(zend_persistent_script *script, void *buf)
{
	UNSERIALIZE_STR(script->script.filename);
	zend_file_cache_unserialize_hash(&script->script.class_table, script, buf, zend_file_cache_unserialize_class, ZEND_CLASS_DTOR);
	zend_file_cache_unserialize_hash(&script->script.function_table, script, buf, zend_file_cache_unserialize_func, NULL);
	zend_file_cache_unserialize_op_array(&script->script.main_op_array, script, buf);
	zend_file_cache_unserialize_hash(&((opkit_persistent_script*)script)->constants_table, script, buf, zend_file_cache_unserialize_constant, NULL);
	zend_file_cache_unserialize_warnings(script, buf);
	zend_file_cache_unserialize_early_bindings(script, buf);
}

/* opkit_compile_script_load
 * Read serialized script data from .phpc file and deserialize back into memory.
 * Includes strict validation of magic number, system_id, and checksum.
 */
zend_persistent_script *opkit_compile_script_load(zend_string *filename)
{
	php_stream *stream;
	zend_persistent_script *script;
	zend_file_cache_metainfo info;
	void *buf;

	stream = php_stream_open_wrapper(ZSTR_VAL(filename), "rb", REPORT_ERRORS, NULL);
	if (!stream) return NULL;

	if (php_stream_read(stream, (char *)&info, sizeof(info)) != sizeof(info)) {
		php_stream_close(stream);
		return NULL;
	}

	if (memcmp(info.magic, "PHPC", 5) != 0 || memcmp(info.system_id, zend_system_id, 32) != 0) {
		php_stream_close(stream);
		return NULL;
	}

	buf = emalloc(info.mem_size + info.str_size);
	if (php_stream_read(stream, buf, info.mem_size + info.str_size) != (size_t)(info.mem_size + info.str_size)) {
		efree(buf);
		php_stream_close(stream);
		return NULL;
	}
	php_stream_close(stream);

	script = (zend_persistent_script *)((char *)buf + info.script_offset);
	script->mem = buf;
	script->size = info.mem_size;

	ZCG(mem) = (void*)((char*)buf + info.mem_size);
	string_pool_base = ZCG(mem);

	zend_file_cache_unserialize(script, buf);

	return script;
}

zend_persistent_script *opkit_compile_file(zend_file_handle *file_handle, int type, zend_op_array **op_array_p)
{
	zend_persistent_script *new_persistent_script;
	uint32_t orig_functions_count, orig_class_count, orig_constants_count;
	zend_op_array *orig_active_op_array;
	zval orig_user_error_handler;
	zend_op_array *op_array;
	bool do_bailout = false;
	uint32_t orig_compiler_options = 0;

	/* Try to open file */
	if (file_handle->type == ZEND_HANDLE_FILENAME) {
		if (zend_stream_open_function(file_handle) != SUCCESS) {
			*op_array_p = NULL;
			if (!EG(exception)) {
				if (type == ZEND_REQUIRE) {
					zend_message_dispatcher(ZMSG_FAILED_REQUIRE_FOPEN, ZSTR_VAL(file_handle->filename));
				} else {
					zend_message_dispatcher(ZMSG_FAILED_INCLUDE_FOPEN, ZSTR_VAL(file_handle->filename));
				}
			}
			return NULL;
		}
	}

	/* Clean holes from previous compilations in global tables to ensure a clean state */
	if (EG(function_table)->nNumUsed != EG(function_table)->nNumOfElements) {
		zend_hash_rehash(EG(function_table));
	}
	if (EG(class_table)->nNumUsed != EG(class_table)->nNumOfElements) {
		zend_hash_rehash(EG(class_table));
	}

	/* Save the original values for the op_array, function table and class table */
	orig_active_op_array = CG(active_op_array);
	orig_functions_count = CG(function_table)->nNumUsed;
	orig_class_count = CG(class_table)->nNumUsed;
	orig_constants_count = EG(zend_constants)->nNumUsed;
	ZVAL_COPY_VALUE(&orig_user_error_handler, &EG(user_error_handler));

	/* Override them with ours */
	ZVAL_UNDEF(&EG(user_error_handler));

	zend_try {
		orig_compiler_options = CG(compiler_options);
		CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;
		CG(compiler_options) |= ZEND_COMPILE_IGNORE_INTERNAL_CLASSES;
		CG(compiler_options) |= ZEND_COMPILE_DELAYED_BINDING;
		// CG(compiler_options) |= ZEND_COMPILE_NO_CONSTANT_SUBSTITUTION;
		CG(compiler_options) |= ZEND_COMPILE_IGNORE_OTHER_FILES;

		op_array = *op_array_p = zend_compile_file(file_handle, type);

		/* Mark newly defined constants as belonging to the current script so they can be moved later */
		if (EG(zend_constants)->nNumUsed > orig_constants_count) {
			Bucket *p = EG(zend_constants)->arData + orig_constants_count;
			Bucket *end = EG(zend_constants)->arData + EG(zend_constants)->nNumUsed;
			for (; p != end; p++) {
				if (Z_TYPE(p->val) != IS_UNDEF) {
					zend_constant *zc = Z_PTR(p->val);
					Z_CONSTANT_FLAGS(zc->value) |= CONST_OWNED_BY_PERSISTENT_SCRIPT;
				}
			}
		}

		CG(compiler_options) = orig_compiler_options;
	} zend_catch {
		op_array = NULL;
		do_bailout = true;
		CG(compiler_options) = orig_compiler_options;
	} zend_end_try();

	/* Restore originals */
	CG(active_op_array) = orig_active_op_array;
	EG(user_error_handler) = orig_user_error_handler;
	EG(record_errors) = 0;

	if (!op_array) {
		/* compilation failed */
		zend_free_recorded_errors();
		if (do_bailout) {
			zend_bailout();
		}
		return NULL;
	}

	/* Build the persistent_script structure.
	   Here we aren't sure we would store it, but we will need it
	   further anyway.
	*/
	new_persistent_script = create_persistent_script();
	new_persistent_script->script.main_op_array = *op_array;

	if (file_handle->opened_path) {
		new_persistent_script->script.filename = zend_string_copy(file_handle->opened_path);
	} else if (file_handle->filename) {
		new_persistent_script->script.filename = zend_string_copy(file_handle->filename);
	}
	if (new_persistent_script->script.filename) {
		zend_string_hash_val(new_persistent_script->script.filename);
	}

	zend_accel_move_user_functions(CG(function_table), CG(function_table)->nNumUsed - orig_functions_count, &new_persistent_script->script);
	zend_accel_move_user_classes(CG(class_table), CG(class_table)->nNumUsed - orig_class_count, &new_persistent_script->script);
	zend_accel_move_user_constants(EG(zend_constants), EG(zend_constants)->nNumUsed - orig_constants_count, new_persistent_script);

	zend_accel_build_delayed_early_binding_list(new_persistent_script);
	new_persistent_script->warnings = zend_persist_warnings(EG(num_errors), EG(errors));
	new_persistent_script->num_warnings = EG(num_errors);

	EG(num_errors) = 0;
	EG(errors) = NULL;

	efree(op_array);
	zend_free_recorded_errors();

	if (EG(exception)) {
		free_persistent_script(new_persistent_script, 1);
		return NULL;
	}

	/* Now persistent_script structure is ready in process memory */
	return new_persistent_script;
}


static zend_string *opkit_get_relative_path(zend_string *path, zend_string *base_path) {
	if (!base_path || ZSTR_LEN(base_path) == 0) {
		return zend_string_copy(path);
	}
	const char *p = ZSTR_VAL(path);
	const char *b = ZSTR_VAL(base_path);
	size_t blen = ZSTR_LEN(base_path);

	if (strncmp(p, b, blen) == 0) {
		const char *rel = p + blen;
		while (*rel == '/') rel++;
		return zend_string_init(rel, strlen(rel), 0);
	}
	return zend_string_copy(path);
}

char *opkit_compile_get_phpc_file_path(zend_string *output_path, zend_string *rel_path)
{
	size_t len;
	char *filename;
	const char *p = ZSTR_VAL(rel_path);
	const char *p_wrapper;

	// Check if rel_path is absolute or has a stream wrapper (e.g., phar://)
	if ((ZSTR_LEN(rel_path) > 0 && IS_SLASH(p[0])) || (strstr(p, "://") && php_stream_locate_url_wrapper(p, &p_wrapper, 0))) {
		filename = emalloc(ZSTR_LEN(rel_path) + sizeof(SUFFIX) + 1);
		memcpy(filename, p, ZSTR_LEN(rel_path) + 1);
	} else {
		if (output_path) {
			len = ZSTR_LEN(output_path);
			filename = emalloc(len + ZSTR_LEN(rel_path) + sizeof(SUFFIX) + 2);
			memcpy(filename, ZSTR_VAL(output_path), len);
			if (len > 0 && filename[len-1] != '/') {
				filename[len++] = '/';
			}
			filename[len] = '\0';
			strcat(filename, p);
		} else {
			filename = emalloc(ZSTR_LEN(rel_path) + sizeof(SUFFIX) + 1);
			memcpy(filename, p, ZSTR_LEN(rel_path) + 1);
		}
	}

	// Replace .php with .phpc if it exists at the end, or skip if already .phpc
	size_t f_len = strlen(filename);
	if (f_len >= 4 && strcmp(filename + f_len - 4, ".php") == 0) {
		filename[f_len - 4] = '\0';
		strcat(filename, SUFFIX);
	} else if (f_len >= strlen(SUFFIX) && strcmp(filename + f_len - strlen(SUFFIX), SUFFIX) == 0) {
		// Already has .phpc, do nothing
	} else {
		strcat(filename, SUFFIX);
	}

	return filename;
}

static int opkit_compile_mkdir(char *filename, size_t start)
{
	size_t len = strlen(filename);
	if (start >= len) {
		start = 0;
	}
	char *s = filename + start;
	struct stat st;

	while (*s) {
		if (IS_SLASH(*s)) {
			char old = *s;
			*s = '\0';
			if (stat(filename, &st) < 0) {
				if (mkdir(filename, S_IRWXU) < 0 && errno != EEXIST) {
					fprintf(stderr, "mkdir failed for %s: %s\n", filename, strerror(errno));
					*s = old;
					return FAILURE;
				}
			} else if (!S_ISDIR(st.st_mode)) {
				fprintf(stderr, "%s exists but is not a directory\n", filename);
				*s = old;
				return FAILURE;
			}
			*s = old;
		}
		s++;
	}
	return SUCCESS;
}

static bool opkit_compile_script_write(int fd, const zend_persistent_script *script, const zend_file_cache_metainfo *info, const void *buf, const zend_string *s)
{
	ssize_t written;
	const ssize_t total_size = (ssize_t)(sizeof(*info) + script->size + info->str_size);

#ifdef HAVE_SYS_UIO_H
	const struct iovec vec[] = {
		{ .iov_base = (void *)info, .iov_len = sizeof(*info) },
		{ .iov_base = (void *)buf, .iov_len = script->size },
		{ .iov_base = (void *)ZSTR_VAL(s), .iov_len = info->str_size },
	};

	written = writev(fd, vec, sizeof(vec) / sizeof(vec[0]));
	if (EXPECTED(written == total_size)) {
		return true;
	}

	errno = written == -1 ? errno : EAGAIN;
	return false;
#else
	if (UNEXPECTED(ZEND_LONG_MAX < (zend_long)total_size)) {
# ifdef EFBIG
		errno = EFBIG;
# else
		errno = ERANGE;
# endif
		return false;
	}

	written = write(fd, info, sizeof(*info));
	if (UNEXPECTED(written != sizeof(*info))) {
		errno = written == -1 ? errno : EAGAIN;
		return false;
	}

	written = write(fd, buf, script->size);
	if (UNEXPECTED(written != script->size)) {
		errno = written == -1 ? errno : EAGAIN;
		return false;
	}

	written = write(fd, ZSTR_VAL(s), info->str_size);
	if (UNEXPECTED(written != info->str_size)) {
		errno = written == -1 ? errno : EAGAIN;
		return false;
	}

	return true;
#endif
}

static zend_always_inline bool is_phar_file(zend_string *filename)
{
	return filename && ZSTR_LEN(filename) >= sizeof(".phar") &&
		!memcmp(ZSTR_VAL(filename) + ZSTR_LEN(filename) - (sizeof(".phar")-1), ".phar", sizeof(".phar")-1) &&
		!strstr(ZSTR_VAL(filename), "://");
}

static void opkit_filename_xlat_dtor(zval *zv) {
	zend_string *str = Z_PTR_P(zv);
	zend_string_release_ex(str, 0);
}

static zend_string *opkit_replace_base_path(zend_string *str, zend_string *base_path) {
	if (!str || ZSTR_LEN(str) == 0) return NULL;

	const char *val = ZSTR_VAL(str);
	const char *base = ZSTR_VAL(base_path);
	size_t base_len = ZSTR_LEN(base_path);

	// Use php_memnstr to handle strings containing NULL bytes (such as RTD keys)
	const char *p = php_memnstr(val, base, base_len, val + ZSTR_LEN(str));
	if (!p) {
		return NULL;
	}

	size_t prefix_len = p - val;
	const char *rel_start = p + base_len;
	while (rel_start < val + ZSTR_LEN(str) && (*rel_start == '/' || *rel_start == '\\')) rel_start++;

	size_t rel_len = (val + ZSTR_LEN(str)) - rel_start;

	zend_string *new_str = zend_string_alloc(prefix_len + rel_len, 0);
	memcpy(ZSTR_VAL(new_str), val, prefix_len);
	memcpy(ZSTR_VAL(new_str) + prefix_len, rel_start, rel_len);
	ZSTR_VAL(new_str)[ZSTR_LEN(new_str)] = '\0';
	zend_string_hash_val(new_str);

	return new_str;
}

static HashTable opkit_filename_xlat;

static zend_string *opkit_get_updated_filename(zend_string *old_str, zend_string *base_path) {
	if (!old_str) return NULL;

	zend_string *cached = zend_hash_index_find_ptr(&opkit_filename_xlat, (uintptr_t)old_str);
	if (cached) {
		return zend_string_copy(cached);
	}

	zend_string *res = opkit_replace_base_path(old_str, base_path);

	if (res) {
		res = zend_new_interned_string(res);
		zend_hash_index_update_ptr(&opkit_filename_xlat, (uintptr_t)old_str, res);
		return zend_string_copy(res);
	}
	return NULL;
}

static void opkit_op_array_update_all_filenames(zend_op_array *op_array, zend_string *base_path) {
	zend_string *res = opkit_get_updated_filename(op_array->filename, base_path);
	if (res) {
		zend_string_release(op_array->filename);
		op_array->filename = res;
	}

	if (op_array->literals) {
		for (int i = 0; i < op_array->last_literal; i++) {
			zval *zv = &op_array->literals[i];
			if (Z_TYPE_P(zv) == IS_STRING) {
				res = opkit_get_updated_filename(Z_STR_P(zv), base_path);
				if (res) {
					zend_string_release(Z_STR_P(zv));
					Z_STR_P(zv) = res;
				}
			}
		}
	}
}

static void opkit_hash_update_filenames(HashTable *ht, zend_string *base_path) {
	Bucket *p;
	uint32_t i;
	bool changed = false;
	for (i = 0; i < ht->nNumUsed; i++) {
		p = ht->arData + i;
		if (Z_TYPE(p->val) == IS_UNDEF) continue;
		if (p->key) {
			zend_string *res = opkit_get_updated_filename(p->key, base_path);
			if (res) {
				zend_string_release(p->key);
				p->key = res;
				p->h = ZSTR_H(res);
				changed = true;
			}
		}
	}
	if (changed) {
		zend_hash_rehash(ht);
	}
}

static void opkit_update_filenames(zend_persistent_script *script, zend_string *base_path) {
	if (!base_path || ZSTR_LEN(base_path) == 0) return;

	zend_hash_init(&opkit_filename_xlat, 32, NULL, (dtor_func_t)opkit_filename_xlat_dtor, 0);

	zend_string *res = opkit_get_updated_filename(script->script.filename, base_path);
	if (res) {
		zend_string_release(script->script.filename);
		script->script.filename = res;
	}

	opkit_op_array_update_all_filenames(&script->script.main_op_array, base_path);

	// Handle HashTables
	opkit_hash_update_filenames(&script->script.function_table, base_path);
	opkit_hash_update_filenames(&script->script.class_table, base_path);

	// Iterate through function table
	zend_function *func;
	ZEND_HASH_FOREACH_PTR(&script->script.function_table, func) {
		if (func->type == ZEND_USER_FUNCTION) {
			opkit_op_array_update_all_filenames(&func->op_array, base_path);
		}
	} ZEND_HASH_FOREACH_END();

	// Iterate through class table
	zend_class_entry *ce;
	ZEND_HASH_FOREACH_PTR(&script->script.class_table, ce) {
		if (ce->type == ZEND_USER_CLASS) {
			res = opkit_get_updated_filename(ce->info.user.filename, base_path);
			if (res) {
				zend_string_release(ce->info.user.filename);
				ce->info.user.filename = res;
			}

				// Only rewrite paths for RTD Keys (starting with \0) to avoid affecting normal class names
			if (ce->name && ZSTR_VAL(ce->name)[0] == '\0') {
				res = opkit_get_updated_filename(ce->name, base_path);
				if (res) {
					zend_string_release(ce->name);
					ce->name = res;
				}
			}

			zend_function *method;
			ZEND_HASH_FOREACH_PTR(&ce->function_table, method) {
				if (method->type == ZEND_USER_FUNCTION) {
					opkit_op_array_update_all_filenames(&method->op_array, base_path);
				}
			} ZEND_HASH_FOREACH_END();
		}
	} ZEND_HASH_FOREACH_END();

	// Handle early bindings
	if (script->early_bindings) {
		for (uint32_t i = 0; i < script->num_early_bindings; i++) {
			res = opkit_get_updated_filename(script->early_bindings[i].lcname, base_path);
			if (res) {
				zend_string_release(script->early_bindings[i].lcname);
				script->early_bindings[i].lcname = res;
			}
			res = opkit_get_updated_filename(script->early_bindings[i].rtd_key, base_path);
			if (res) {
				zend_string_release(script->early_bindings[i].rtd_key);
				script->early_bindings[i].rtd_key = res;
			}
			res = opkit_get_updated_filename(script->early_bindings[i].lc_parent_name, base_path);
			if (res) {
				zend_string_release(script->early_bindings[i].lc_parent_name);
				script->early_bindings[i].lc_parent_name = res;
			}
		}
	}

	zend_hash_destroy(&opkit_filename_xlat);
}

/* opkit_compile_script_store
 * Serialize the persistent script and write it to a .phpc file.
 * Before writing, if a base_path is provided, absolute paths in the script will be replaced with relative paths.
 */
int opkit_compile_script_store(zend_string *output_path, zend_persistent_script *script, zend_string *base_path)
{
	zend_persistent_script *orig_script = script;
	zend_string *orig_filename = script->script.filename;
	zend_string *rel_filename = NULL;

	zend_string_addref(orig_filename);

	if (base_path && ZSTR_LEN(base_path) > 0) {
		rel_filename = opkit_get_relative_path(orig_filename, base_path);
		opkit_update_filenames(script, base_path);
	}

	int fd;
	char *filename;
	char *tmp_filename = NULL;
	zend_file_cache_metainfo info;
	void *buf;

	uint32_t memory_used;

	zend_shared_alloc_init_xlat_table();

	/* Calculate the required memory size */
	memory_used = zend_accel_script_persist_calc(script, 0);

	/* Allocate memory block */
	void *mem_to_free = emalloc(memory_used + 64);
	ZCG(mem) = (void *)(((uintptr_t)mem_to_free + 63L) & ~63L);

	zend_shared_alloc_clear_xlat_table();

	/* Initialize interned strings pool */
	info.str_size = 0;
	ZCG(current_persistent_script) = script;

	/* Copy into memory block */
	zend_persistent_script *persisted_script = zend_accel_script_persist(script, 0);

	if (!persisted_script) {
		zend_accel_error(ACCEL_LOG_ERROR, "Failed to persist script: %s\n", ZSTR_VAL(orig_filename));
		efree(mem_to_free);
		goto store_failure;
	}

	persisted_script->is_phar = is_phar_file(persisted_script->script.filename);

	/* Consistency check */
	if (persisted_script->size != (uint32_t)((char*)ZCG(mem) - (char*)persisted_script->mem)) {
		zend_accel_error(
			(persisted_script->size < (uint32_t)((char*)ZCG(mem) - (char*)persisted_script->mem)) ? ACCEL_LOG_ERROR : ACCEL_LOG_WARNING,
			"Internal error: wrong size calculation: %s start=" ZEND_ADDR_FMT ", end=" ZEND_ADDR_FMT ", real=" ZEND_ADDR_FMT ", calc_size=%u, real_size=%u\n",
			ZSTR_VAL(orig_filename),
			(size_t)persisted_script->mem,
			(size_t)((char *)persisted_script->mem + persisted_script->size),
			(size_t)ZCG(mem),
			(uint32_t)persisted_script->size,
			(uint32_t)((char*)ZCG(mem) - (char*)persisted_script->mem));
	}

	script = persisted_script;

	filename = opkit_compile_get_phpc_file_path(output_path, rel_filename ? rel_filename : orig_filename);

	if (opkit_compile_mkdir(filename, ZSTR_LEN(output_path)) != SUCCESS) {
		zend_accel_error(ACCEL_LOG_WARNING, "opkit cannot create directory for file '%s', %s\n", filename, strerror(errno));
		efree(mem_to_free);
		efree(filename);
		goto store_failure;
	}

	/* Use temporary file for atomic write */
	spprintf(&tmp_filename, 0, "%s.tmp.%d", filename, (int)getpid());

	fd = opkit_compile_open(tmp_filename, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		zend_accel_error(ACCEL_LOG_WARNING, "opkit cannot create temporary file '%s', %s\n", tmp_filename, strerror(errno));
		efree(mem_to_free);
		efree(filename);
		efree(tmp_filename);
		goto store_failure;
	}

	if (opkit_compile_flock(fd, LOCK_EX) != 0) {
		close(fd);
		efree(mem_to_free);
		opkit_compile_unlink(tmp_filename);
		efree(filename);
		efree(tmp_filename);
		goto store_failure;
	}

#if defined(__AVX__) || defined(__SSE2__)
	/* Alignment to 64-byte boundary */
	void *buf_mem_to_free = emalloc(memory_used + 64);
	buf = (void*)(((uintptr_t)buf_mem_to_free + 63L) & ~63L);
#else
	void *buf_mem_to_free = buf = emalloc(memory_used);
#endif

	memset(buf, 0, memory_used);

	current_string_pool = zend_string_alloc(memory_used + 1024 * 1024, 0);
	string_pool_base = (void*)ZSTR_VAL(current_string_pool);

	zend_file_cache_metainfo *info_p = &info;

	memset(info_p, 0, sizeof(info));
	memcpy(info_p->magic, "PHPC", 5);
	memcpy(info_p->system_id, zend_system_id, 32);
	info_p->mem_size = script->size;
	info_p->str_size = 0;
	info_p->metadata_size = opkit_metadata_size;
	info_p->code_size = opkit_code_size;
	info_p->data_size = opkit_data_size;
	info_p->misc_size = opkit_misc_size;
	info_p->script_offset = (char*)script - (char*)script->mem;
	info_p->timestamp = time(NULL);

	/* Clean up orig_script while xlat_table is still available.
	 * zend_accel_script_persist already released the interned strings it moved. */
	if (orig_script->script.filename && !zend_shared_alloc_get_xlat_entry(orig_script->script.filename)) {
		zend_string_release(orig_script->script.filename);
	}
	efree(orig_script);
	orig_script = NULL;

	memcpy(buf, script->mem, script->size);

	zend_shared_alloc_clear_xlat_table();
	zend_file_cache_serialize((zend_persistent_script*)((char*)buf + info_p->script_offset), info_p, buf);

	info_p->checksum = zend_adler32(ADLER32_INIT, (unsigned char*)buf, info_p->mem_size);
	info_p->checksum = zend_adler32(info_p->checksum, (unsigned char*)ZSTR_VAL(current_string_pool), info_p->str_size);

	if (!opkit_compile_script_write(fd, script, info_p, buf, current_string_pool)) {
		zend_accel_error(ACCEL_LOG_WARNING, "opkit cannot write to temporary file '%s': %s\n", tmp_filename, strerror(errno));
		zend_string_release_ex(current_string_pool, 0);
		current_string_pool = NULL;
		close(fd);
		efree(buf_mem_to_free);
		opkit_compile_unlink(tmp_filename);
		efree(filename);
		efree(tmp_filename);
		efree(mem_to_free);
		goto store_failure;
	}

	// At this point ZCG(mem) still points to the end position of mem_to_free, used for validation
	script->size = (uint32_t)((char*)ZCG(mem) - (char*)script->mem);

	if (rel_filename) {
		zend_string_release(rel_filename);
	}

	zend_string_release(orig_filename);

	zend_shared_alloc_destroy_xlat_table();

	zend_string_release_ex(current_string_pool, 0);
	current_string_pool = NULL;
	ZCG(mem) = NULL;
	ZCG(current_persistent_script) = NULL;

	if (opkit_compile_flock(fd, LOCK_UN) != 0) {
		zend_accel_error(ACCEL_LOG_WARNING, "opkit cannot unlock temporary file '%s': %s\n", tmp_filename, strerror(errno));
	}
	close(fd);

	/* Atomic rename to target file */
	if (rename(tmp_filename, filename) != 0) {
		zend_accel_error(ACCEL_LOG_WARNING, "opkit cannot rename temporary file '%s' to '%s': %s\n", tmp_filename, filename, strerror(errno));
		opkit_compile_unlink(tmp_filename);
		efree(filename);
		efree(tmp_filename);
		efree(buf_mem_to_free);
		goto store_failure;
	}

	efree(filename);
	efree(tmp_filename);
	efree(buf_mem_to_free);

	opkit_keep_memory(script, mem_to_free);

	return SUCCESS;

store_failure:
	if (rel_filename) {
		zend_string_release(rel_filename);
	}
	if (orig_script) {
		if (orig_script->script.filename && !zend_shared_alloc_get_xlat_entry(orig_script->script.filename)) {
			zend_string_release(orig_script->script.filename);
		}
		efree(orig_script);
	}
	zend_string_release(orig_filename);
	zend_shared_alloc_destroy_xlat_table();
	ZCG(current_persistent_script) = NULL;
	ZCG(mem) = NULL;
	return FAILURE;
}
