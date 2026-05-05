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
#include "php_opkit.h"
#include "opkit_module.h"
#include "opkit_compile.h"
#include "opkit_wrapper.h"
zend_string_table opkit_interned_strings;
#include "opkit_util_funcs.h"
#include "opkit_arginfo.h"
#include "php_main.h"
#include "ext/standard/info.h"
#include "Optimizer/zend_optimizer.h"
#include "php_streams.h"
#include "Zend/zend_vm.h"
#include "Zend/zend_virtual_cwd.h"
#include "Zend/zend_inheritance.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_exceptions.h"
#include "zend_system_id.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define OPKIT_GET_PTR(buf, ptr) ((ptr) ? (void*)((char*)(buf) + (uintptr_t)(ptr)) : NULL)

typedef struct _opkit_script_node {
	zend_persistent_script *script;
	void *mem_to_free;
	zend_string *loaded_path;
	bool executed;
	struct _opkit_script_node *next;
} opkit_script_node;

static opkit_script_node *loaded_scripts = NULL;
static void opkit_fix_op_array_filenames(zend_op_array *op_array, zend_string *phar_prefix);

/* Class linking error info structure (based on preload_error from ZendAccelerator) */
typedef struct _opkit_link_error {
	const char *kind;
	const char *name;
} opkit_link_error;

/* Forward declarations for class linking functions */
static zend_result opkit_resolve_class_deps(opkit_link_error *error, const zend_class_entry *ce);
static void opkit_link_classes(void);
static bool opkit_try_resolve_class_constants(zend_class_entry *ce);
static void opkit_resolve_all_class_constants(void);

void opkit_clean_script_items(zend_persistent_script *script) {
	if (!script) return;

	zend_string *key;
	zend_function *f;
	zend_class_entry *ce;
	dtor_func_t orig_dtor;
	opkit_persistent_script *op_script = (opkit_persistent_script *)script;

	// 1. Clean up functions
	if (EG(function_table)) {
		orig_dtor = EG(function_table)->pDestructor;
		EG(function_table)->pDestructor = NULL;
		ZEND_HASH_FOREACH_PTR(&script->script.function_table, f) {
			if (f && f->type == ZEND_USER_FUNCTION && f->common.function_name) {
				zend_string *lcname = zend_string_tolower(f->common.function_name);
				void *current_f = zend_hash_find_ptr(EG(function_table), lcname);
				if (current_f == (void*)f) {
					zend_hash_del(EG(function_table), lcname);
				}
				zend_string_release(lcname);
			}
		} ZEND_HASH_FOREACH_END();
		EG(function_table)->pDestructor = orig_dtor;
	}

	// 2. Clean up classes
	if (EG(class_table)) {
		orig_dtor = EG(class_table)->pDestructor;
		EG(class_table)->pDestructor = NULL;
		ZEND_HASH_FOREACH_STR_KEY_PTR(&script->script.class_table, key, ce) {
			if (!ce) continue;
			// 2.1 Clean up original keys (RTD key or ce->name)
			if (key) {
				void *current_ce = zend_hash_find_ptr(EG(class_table), key);
				if (current_ce == (void*)ce) {
					zend_hash_del(EG(class_table), key);
				}
			} else if (ce->name) {
				void *current_ce = zend_hash_find_ptr(EG(class_table), ce->name);
				if (current_ce == (void*)ce) {
					zend_hash_del(EG(class_table), ce->name);
				}
			}
			// 2.2 Clean up lowercase keys
			if (ce->name) {
				zend_string *lcname = zend_string_tolower(ce->name);
				void *current_ce = zend_hash_find_ptr(EG(class_table), lcname);
				if (current_ce == (void*)ce) {
					zend_hash_del(EG(class_table), lcname);
				}
				zend_string_release(lcname);
			}
		} ZEND_HASH_FOREACH_END();
		EG(class_table)->pDestructor = orig_dtor;
	}

	// 3. Clean up constants
	if (EG(zend_constants)) {
		orig_dtor = EG(zend_constants)->pDestructor;
		EG(zend_constants)->pDestructor = NULL;
		zend_constant *zc;
		ZEND_HASH_FOREACH_PTR(&op_script->constants_table, zc) {
			if (zc->name) {
				void *current_zc = zend_hash_find_ptr(EG(zend_constants), zc->name);
				if (current_zc == (void*)zc) {
					zend_hash_del(EG(zend_constants), zc->name);
				}
			}
		} ZEND_HASH_FOREACH_END();
		EG(zend_constants)->pDestructor = orig_dtor;
	}

	/* Runtime cache cleanup disabled - causes crashes with invalid pointers
	 * The memory is freed anyway when the entire script memory is freed
	 */
}

void opkit_keep_memory(zend_persistent_script *script, void *mem_to_free, zend_string *path) {
	opkit_script_node *node = emalloc(sizeof(opkit_script_node));
	node->script = script;
	node->mem_to_free = mem_to_free;
	node->loaded_path = path ? zend_string_init(ZSTR_VAL(path), ZSTR_LEN(path), 0) : NULL;
	node->executed = false;
	node->next = loaded_scripts;
	loaded_scripts = node;
}

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
		if (node->loaded_path) {
			zend_string_release(node->loaded_path);
		}
		efree(node);
		node = next;
	}
	loaded_scripts = NULL;
}

/* opkit_resolve_path
 * Process relative paths and try to resolve them to absolute paths.
 * If currently in a Phar execution environment, it will first try to fix paths relative to the Phar interior.
 */
static zend_string *opkit_resolve_path(zend_string *path) {
	if (path == NULL || ZSTR_LEN(path) == 0) {
		return NULL;
	}

	char *path_val = ZSTR_VAL(path);

	// Check if path already has a stream wrapper (e.g., phar://)
	if (strstr(path_val, "://")) {
		return zend_string_copy(path);
	}

	if (IS_ABSOLUTE_PATH(path_val, ZSTR_LEN(path))) {
		char realpath_buf[MAXPATHLEN];
		if (VCWD_REALPATH(path_val, realpath_buf)) {
			return zend_string_init(realpath_buf, strlen(realpath_buf), 0);
		}
		return zend_string_copy(path);
	}

	// Relative path, try to get the directory of the currently executing script
	zend_string *current_script = zend_get_executed_filename_ex();
	if (current_script && ZSTR_LEN(current_script) > 0) {
		char *dir = estrndup(ZSTR_VAL(current_script), ZSTR_LEN(current_script));
		char *s = dir + ZSTR_LEN(current_script) - 1;
		while (s >= dir) {
			if (IS_SLASH(*s)) {
				*s = '\0';
				break;
			}
			s--;
		}

		zend_string *resolved_path;
		if (s >= dir) {
			// Found directory
			size_t dir_len = strlen(dir);
			size_t path_len = ZSTR_LEN(path);
			char *combined = emalloc(dir_len + 1 + path_len + 1);
			memcpy(combined, dir, dir_len);
			combined[dir_len] = '/';
			memcpy(combined + dir_len + 1, path_val, path_len);
			combined[dir_len + 1 + path_len] = '\0';

			char realpath_buf[MAXPATHLEN];
			if (!strstr(combined, "://") && VCWD_REALPATH(combined, realpath_buf)) {
				resolved_path = zend_string_init(realpath_buf, strlen(realpath_buf), 0);
			} else {
				resolved_path = zend_string_init(combined, dir_len + 1 + path_len, 0);
			}
			efree(combined);
		} else {
			// No slash found, fallback to CWD
			char realpath_buf[MAXPATHLEN];
			if (VCWD_REALPATH(path_val, realpath_buf)) {
				resolved_path = zend_string_init(realpath_buf, strlen(realpath_buf), 0);
			} else {
				resolved_path = zend_string_copy(path);
			}
		}
		efree(dir);
		return resolved_path;
	} else {
		// Cannot get current script, fallback to CWD
		char realpath_buf[MAXPATHLEN];
		if (VCWD_REALPATH(path_val, realpath_buf)) {
			return zend_string_init(realpath_buf, strlen(realpath_buf), 0);
		}
		return zend_string_copy(path);
	}
}

static bool opkit_do_compile_file(zend_string *output_path, zend_string *script_name, zend_string *base_path) {
	zend_file_handle file_handle;
	zend_op_array *op_array = NULL;
	zend_execute_data *orig_execute_data = NULL;
	uint32_t orig_compiler_options;
	zend_persistent_script *persistent_script;
	bool success = false;

	zend_stream_init_filename_ex(&file_handle, script_name);

	orig_execute_data = EG(current_execute_data);
	orig_compiler_options = CG(compiler_options);
	CG(compiler_options) |= ZEND_COMPILE_WITHOUT_EXECUTION;
	CG(compiler_options) &= ~ZEND_COMPILE_PRELOAD;

	zend_try {
		persistent_script = opkit_compile_file(&file_handle, ZEND_REQUIRE, &op_array);
		if (persistent_script) {
			opkit_compile_script_store(output_path, persistent_script, base_path);
			/* Free the original persistent_script to prevent access to corrupted
			 * default_properties_table pointers. The persisted copy is in the file. */
			// free_persistent_script(persistent_script, 0);
			success = true;
		}
	} zend_catch {
		EG(current_execute_data) = orig_execute_data;
		zend_error(E_WARNING, OPKIT_EXTENSION_NAME " could not compile file %s", ZSTR_VAL(file_handle.filename));
	} zend_end_try();

	CG(compiler_options) = orig_compiler_options;
	zend_destroy_file_handle(&file_handle);
	return success;
}

static void opkit_do_compile_dir(zend_string *output_path, zend_string *dir_path, zend_string *base_path) {
	char *path = ZSTR_VAL(dir_path);
	DIR *dir = opendir(path);
	if (!dir) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

		char full_path[MAXPATHLEN];
		snprintf(full_path, MAXPATHLEN, "%s/%s", path, entry->d_name);

		struct stat st;
		if (stat(full_path, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				zend_string *next_dir = zend_string_init(full_path, strlen(full_path), 0);
				opkit_do_compile_dir(output_path, next_dir, base_path);
				zend_string_release(next_dir);
			} else if (S_ISREG(st.st_mode)) {
				const char *ext = strrchr(entry->d_name, '.');
				if (ext && strcmp(ext, ".php") == 0) {
					zend_string *file_to_compile = zend_string_init(full_path, strlen(full_path), 0);
					opkit_do_compile_file(output_path, file_to_compile, base_path);
					zend_string_release(file_to_compile);
				}
			}
		}
	}
	closedir(dir);
}

// Generate opcache bin file
ZEND_FUNCTION(opkit_compile_file) {
	zend_string *output_path;
	zend_string *script_name;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "SS", &output_path, &script_name) == FAILURE) {
		RETURN_THROWS();
	}

	zend_string *resolved_output_path = opkit_resolve_path(output_path);
	zend_string *resolved_script_path = opkit_resolve_path(script_name);

	char *p = ZSTR_VAL(resolved_script_path);
	char *slash = p + ZSTR_LEN(resolved_script_path);
	while (--slash >= p && !IS_SLASH(*slash));
	zend_string *base_path = NULL;
	if (slash >= p) {
		size_t dir_len = slash - p;
		if (dir_len == 0) { // Root directory "/"
			base_path = zend_string_init("/", 1, 0);
		} else {
			base_path = zend_string_init(p, dir_len, 0);
		}
	} else {
		char cwd_buf[MAXPATHLEN];
		char *cwd = VCWD_GETCWD(cwd_buf, MAXPATHLEN);
		base_path = zend_string_init(cwd, strlen(cwd), 0);
	}

	if (opkit_do_compile_file(resolved_output_path, resolved_script_path, base_path)) {
		zend_string_release(resolved_output_path);
		zend_string_release(resolved_script_path);
		zend_string_release(base_path);
		opkit_reset_script();
		RETURN_TRUE;
	} else {
		zend_string_release(resolved_output_path);
		zend_string_release(resolved_script_path);
		zend_string_release(base_path);
		opkit_reset_script();
		RETURN_FALSE;
	}
}

ZEND_FUNCTION(opkit_compile_dir) {
	zend_string *output_path;
	zend_string *dir_name;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "SS", &output_path, &dir_name) == FAILURE) {
		RETURN_THROWS();
	}

	zend_string *resolved_output_path = opkit_resolve_path(output_path);
	zend_string *resolved_dir_path = opkit_resolve_path(dir_name);

	if (!resolved_dir_path) {
		zend_error(E_WARNING, OPKIT_EXTENSION_NAME " could not resolve path %s", ZSTR_VAL(dir_name));
		zend_string_release(resolved_output_path);
		RETURN_FALSE;
	}

	opkit_do_compile_dir(resolved_output_path, resolved_dir_path, resolved_dir_path);
	opkit_reset_script();

	zend_string_release(resolved_dir_path);
	zend_string_release(resolved_output_path);
	RETURN_TRUE;
}

// Load .phpc file
ZEND_FUNCTION(opkit_load) {
	zend_string *filename;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &filename) == FAILURE) {
		RETURN_THROWS();
	}

	zend_string *resolved_path = opkit_resolve_path(filename);
	char *full_path = opkit_compile_get_phpc_file_path(NULL, resolved_path);
	zend_string *path = zend_string_init(full_path, strlen(full_path), 0);
	efree(full_path);
	zend_string_release(resolved_path);

	zend_persistent_script *loaded_script = opkit_compile_script_load(path);
	zend_string_release(path);

	if (loaded_script) {
		opkit_keep_memory(loaded_script, loaded_script->mem, path);
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

// Batch load .phpc files
ZEND_FUNCTION(opkit_load_multi) {
	HashTable *filenames;
	zval *entry;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "h", &filenames) == FAILURE) {
		RETURN_THROWS();
	}

	ZEND_HASH_FOREACH_VAL(filenames, entry) {
		if (Z_TYPE_P(entry) != IS_STRING) {
			continue;
		}
		zend_string *filename = Z_STR_P(entry);
		zend_string *resolved_path = opkit_resolve_path(filename);
		if (!resolved_path) {
			continue;
		}
		char *full_path = opkit_compile_get_phpc_file_path(NULL, resolved_path);
		zend_string *path = zend_string_init(full_path, strlen(full_path), 0);
		efree(full_path);
		zend_string_release(resolved_path);

		zend_persistent_script *loaded_script = opkit_compile_script_load(path);

		if (loaded_script) {
			opkit_keep_memory(loaded_script, loaded_script->mem, path);
		}
		zend_string_release(path);
	} ZEND_HASH_FOREACH_END();
}

static void add_mem_block(zval *structure, const char *name, size_t offset, size_t size) {
	zval block;
	array_init(&block);
	add_assoc_string(&block, "name", (char*)name);
	add_assoc_long(&block, "offset", offset);
	add_assoc_long(&block, "size", size);
	add_assoc_long(&block, "start", offset);
	add_assoc_long(&block, "end", offset + size);
	add_next_index_zval(structure, &block);
}

#define IS_IN_RANGE(ptr, size) ((uintptr_t)(ptr) > 0 && (uintptr_t)(ptr) < (size))
#define IS_IN_BUF_RANGE(ptr, buf, size) ((char*)(ptr) >= (char*)(buf) && (char*)(ptr) < (char*)(buf) + (size))

static zend_string *opkit_get_str_from_pool(char *buf, zend_string *ptr, size_t mem_size, size_t total_size) {
	if (!ptr) return NULL;
	uintptr_t uptr = (uintptr_t)ptr;
	zend_string *s;
	if (uptr & 1) { // Interned
		uintptr_t offset = uptr & ~1;
		s = (zend_string *)(buf + mem_size + offset);
	} else { // Normal
		s = (zend_string *)(buf + uptr);
	}
	// Validation
	if (!IS_IN_BUF_RANGE(s, buf, total_size)) return NULL;
	if ((char*)s + sizeof(zend_string) > buf + total_size) return NULL;
	if (ZSTR_LEN(s) > total_size) return NULL; // Prevent excessively long strings
	if ((char*)s + ZSTR_LEN(s) > buf + total_size) return NULL;
	return s;
}

static void opkit_get_type_name(smart_str *str, zend_type type, char *buf, size_t mem_size, size_t total_size) {
	if (!ZEND_TYPE_IS_SET(type)) {
		return;
	}

	if (ZEND_TYPE_IS_UNION(type) || ZEND_TYPE_IS_INTERSECTION(type)) {
		smart_str_appends(str, "mixed");
		return;
	}

	if (ZEND_TYPE_HAS_NAME(type)) {
		zend_string *name = (zend_string *)OPKIT_GET_PTR(buf, ZEND_TYPE_NAME(type));
		zend_string *pool_name = opkit_get_str_from_pool(buf, name, mem_size, total_size);
		if (pool_name) {
			smart_str_appends(str, ZSTR_VAL(pool_name));
		} else {
			smart_str_appends(str, "unknown");
		}
	} else {
		uint32_t type_mask = ZEND_TYPE_PURE_MASK(type);
		if (type_mask == MAY_BE_ANY) {
			smart_str_appends(str, "mixed");
		} else if (type_mask & MAY_BE_STRING) {
			smart_str_appends(str, "string");
		} else if (type_mask & MAY_BE_LONG) {
			smart_str_appends(str, "int");
		} else if (type_mask & MAY_BE_DOUBLE) {
			smart_str_appends(str, "float");
		} else if (type_mask & MAY_BE_BOOL) {
			smart_str_appends(str, "bool");
		} else if (type_mask & MAY_BE_ARRAY) {
			smart_str_appends(str, "array");
		} else if (type_mask & MAY_BE_OBJECT) {
			smart_str_appends(str, "object");
		} else if (type_mask & MAY_BE_CALLABLE) {
			smart_str_appends(str, "callable");
		} else if (type_mask & MAY_BE_VOID) {
			smart_str_appends(str, "void");
		} else if (type_mask & MAY_BE_NEVER) {
			smart_str_appends(str, "never");
		} else if (type_mask & MAY_BE_NULL) {
			smart_str_appends(str, "null");
		}
	}

	if (ZEND_TYPE_ALLOW_NULL(type)) {
		smart_str_appends(str, "|null");
	}
}

static void opkit_get_function_signature(smart_str *str, zend_function *f, char *buf, size_t mem_size, size_t total_size)
{
	uint32_t i;
	uint32_t num_args = f->common.num_args;
	zend_arg_info *arg_info = (zend_arg_info *)OPKIT_GET_PTR(buf, f->common.arg_info);

	smart_str_appends(str, "(");
	for (i = 0; i < num_args; i++) {
		if (i > 0) smart_str_appends(str, ", ");
		if (arg_info && IS_IN_BUF_RANGE(&arg_info[i], buf, total_size)) {
			opkit_get_type_name(str, arg_info[i].type, buf, mem_size, total_size);
			if (ZEND_TYPE_IS_SET(arg_info[i].type)) {
				smart_str_appends(str, " ");
			}

			zend_string *name = opkit_get_str_from_pool(buf, arg_info[i].name, mem_size, total_size);
			if (name) {
				smart_str_appends(str, "$");
				smart_str_appends(str, ZSTR_VAL(name));
			} else {
				smart_str_appends(str, "$arg");
			}
		} else {
			smart_str_appends(str, "$arg");
		}
	}
	smart_str_appends(str, ")");

	if (f->common.fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
		zend_arg_info *ret_info = arg_info - 1;
		if (IS_IN_BUF_RANGE(ret_info, buf, total_size)) {
			smart_str_appends(str, ": ");
			opkit_get_type_name(str, ret_info->type, buf, mem_size, total_size);
		}
	}

	smart_str_0(str);
}

// Get opcode file information
ZEND_FUNCTION(opkit_get_info)
{
	zend_string *filename;
	zend_string *resolved_filename;
	int fd;
	zend_file_cache_metainfo info;
	char *buf;
	zend_persistent_script *script;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(filename)
	ZEND_PARSE_PARAMETERS_END();

	resolved_filename = opkit_resolve_path(filename);
	if (!resolved_filename) {
		RETURN_NULL();
	}

	fd = open(ZSTR_VAL(resolved_filename), O_RDONLY | O_BINARY);
	zend_string_release(resolved_filename);

	if (fd < 0) {
		RETURN_NULL();
	}

	if (read(fd, &info, sizeof(info)) != sizeof(info)) {
		close(fd);
		RETURN_NULL();
	}

	if (memcmp(info.magic, "PHPC", 5) != 0) {
		close(fd);
		RETURN_NULL();
	}

	array_init(return_value);
	add_assoc_stringl(return_value, "magic", info.magic, 8);
	char system_id[33];
	memcpy(system_id, info.system_id, 32);
	system_id[32] = '\0';
	add_assoc_string(return_value, "system_id", system_id);
	add_assoc_long(return_value, "mem_size", (zend_long)info.mem_size);
	add_assoc_long(return_value, "str_size", (zend_long)info.str_size);
	add_assoc_long(return_value, "timestamp", (zend_long)info.timestamp);
	add_assoc_long(return_value, "checksum", (zend_long)info.checksum);

	/* If system_id does not match, we cannot safely deserialize the entire script, but can return basic information */
	if (memcmp(info.system_id, zend_system_id, 32) != 0) {
		add_assoc_bool(return_value, "system_id_match", 0);
		close(fd);
		return;
	}
	add_assoc_bool(return_value, "system_id_match", 1);

	/* Read the remaining part to get script details */
	buf = emalloc(info.mem_size + info.str_size);
	if (read(fd, buf, info.mem_size + info.str_size) != (ssize_t)(info.mem_size + info.str_size)) {
		efree(buf);
		close(fd);
		return;
	}
	close(fd);

	script = (zend_persistent_script *)((char *)buf + info.script_offset);

	/* Note: Fields like nNumOfElements in HashTable are readable even without deserialization */
	add_assoc_long(return_value, "num_functions", script->script.function_table.nNumOfElements);
	add_assoc_long(return_value, "num_classes", script->script.class_table.nNumOfElements);
	add_assoc_long(return_value, "num_early_bindings", script->num_early_bindings);

	add_assoc_long(return_value, "metadata_size", info.metadata_size);
	add_assoc_long(return_value, "code_size", info.code_size);
	add_assoc_long(return_value, "data_size", info.data_size);
	add_assoc_long(return_value, "misc_size", info.misc_size);

	/* Add memory structure information */
	zval structure;
	array_init(&structure);
	size_t total_size = info.mem_size + info.str_size;
	size_t header_size = sizeof(info);

	add_mem_block(&structure, "Metainfo", 0, header_size);
	add_mem_block(&structure, "Persistent Script", header_size + info.script_offset, sizeof(opkit_persistent_script));

	// Main OpArray
	add_mem_block(&structure, "Main OpArray", header_size + ((char*)&script->script.main_op_array - buf), sizeof(zend_op_array));

	if (IS_IN_RANGE(script->script.main_op_array.opcodes, total_size)) {
		add_mem_block(&structure, "Main OpCodes", header_size + (uintptr_t)script->script.main_op_array.opcodes,
					  script->script.main_op_array.last * sizeof(zend_op));
	}

	if (IS_IN_RANGE(script->script.main_op_array.literals, total_size)) {
		add_mem_block(&structure, "Main Literals", header_size + (uintptr_t)script->script.main_op_array.literals,
					  script->script.main_op_array.last_literal * sizeof(zval));
	}

	if (IS_IN_RANGE(script->script.main_op_array.arg_info, total_size)) {
		add_mem_block(&structure, "Main ArgInfo", header_size + (uintptr_t)script->script.main_op_array.arg_info,
					  (script->script.main_op_array.num_args + (script->script.main_op_array.fn_flags & ZEND_ACC_HAS_RETURN_TYPE ? 1 : 0)) * sizeof(zend_arg_info));
	}

	// Function Table Data
	if (IS_IN_RANGE(script->script.function_table.arData, total_size)) {
		void *data_addr = HT_GET_DATA_ADDR(&script->script.function_table);
		add_mem_block(&structure, "Functions Table Data", header_size + (uintptr_t)data_addr,
					  ZEND_ALIGNED_SIZE(HT_USED_SIZE(&script->script.function_table)));
	}

	// Class Table Data
	if (IS_IN_RANGE(script->script.class_table.arData, total_size)) {
		void *data_addr = HT_GET_DATA_ADDR(&script->script.class_table);
		add_mem_block(&structure, "Classes Table Data", header_size + (uintptr_t)data_addr,
					  ZEND_ALIGNED_SIZE(HT_USED_SIZE(&script->script.class_table)));
	}

	// Early Bindings
	if (script->num_early_bindings && IS_IN_RANGE(script->early_bindings, total_size)) {
		add_mem_block(&structure, "Early Bindings", header_size + (uintptr_t)script->early_bindings,
					  script->num_early_bindings * sizeof(zend_early_binding));
	}

	// Iterate through functions and classes in detail to add memory block information
	Bucket *pb, *endb;
	if (IS_IN_RANGE(script->script.function_table.arData, total_size)) {
		pb = (Bucket*)((char*)buf + (uintptr_t)script->script.function_table.arData);
		endb = pb + script->script.function_table.nNumUsed;
		while (pb < endb) {
			if (Z_TYPE(pb->val) != IS_UNDEF && Z_TYPE(pb->val) == IS_PTR) {
				zend_function *f = (zend_function*)((char*)buf + (uintptr_t)Z_PTR(pb->val));
				zend_string *s = opkit_get_str_from_pool(buf, f->common.function_name, info.mem_size, total_size);
				if (s) {
					char block_name[128];
					snprintf(block_name, sizeof(block_name), "Function: %s", ZSTR_VAL(s));
					add_mem_block(&structure, block_name, header_size + (uintptr_t)Z_PTR(pb->val), sizeof(zend_op_array));
				}
			}
			pb++;
		}
	}

	if (IS_IN_RANGE(script->script.class_table.arData, total_size)) {
		pb = (Bucket*)((char*)buf + (uintptr_t)script->script.class_table.arData);
		endb = pb + script->script.class_table.nNumUsed;
		while (pb < endb) {
			if (Z_TYPE(pb->val) != IS_UNDEF && Z_TYPE(pb->val) == IS_PTR) {
				zend_class_entry *ce = (zend_class_entry*)((char*)buf + (uintptr_t)Z_PTR(pb->val));
				zend_string *s = opkit_get_str_from_pool(buf, ce->name, info.mem_size, total_size);
				if (s) {
					char block_name[128];
					snprintf(block_name, sizeof(block_name), "Class: %s", ZSTR_VAL(s));
					add_mem_block(&structure, block_name, header_size + (uintptr_t)Z_PTR(pb->val), sizeof(zend_class_entry));
				}
			}
			pb++;
		}
	}

	// String Pool
	add_mem_block(&structure, "String Pool", header_size + info.mem_size, info.str_size);

	add_assoc_zval(return_value, "structure", &structure);

	add_assoc_long(return_value, "metadata_size", info.metadata_size);
	add_assoc_long(return_value, "code_size", info.code_size);
	add_assoc_long(return_value, "data_size", info.data_size);
	add_assoc_long(return_value, "misc_size", info.misc_size);

	/* Collect symbol tables (functions and classes) */
	zval functions, classes;
	array_init(&functions);
	array_init(&classes);

	Bucket *p, *end;
	if (IS_IN_RANGE(script->script.function_table.arData, total_size)) {
		p = (Bucket*)((char*)buf + (uintptr_t)script->script.function_table.arData);
		end = p + script->script.function_table.nNumUsed;
		while (p < end) {
			if (Z_TYPE(p->val) != IS_UNDEF) {
				zend_string *s = NULL;
				zend_function *f = NULL;
				if (Z_TYPE(p->val) == IS_PTR) {
					f = (zend_function*)((char*)buf + (uintptr_t)Z_PTR(p->val));
					if (IS_IN_BUF_RANGE(f, buf, total_size)) {
						s = opkit_get_str_from_pool(buf, f->common.function_name, info.mem_size, total_size);
					}
				}
				if (!s) {
					s = opkit_get_str_from_pool(buf, p->key, info.mem_size, total_size);
				}
				if (s) {
					zval func_info;
					array_init(&func_info);
					add_assoc_stringl(&func_info, "name", ZSTR_VAL(s), ZSTR_LEN(s));

					if (f && IS_IN_BUF_RANGE(f, buf, total_size)) {
						smart_str sig = {0};
						opkit_get_function_signature(&sig, f, buf, info.mem_size, total_size);
						if (sig.s) {
							add_assoc_stringl(&func_info, "signature", ZSTR_VAL(sig.s), ZSTR_LEN(sig.s));
							smart_str_free(&sig);
						}
					}
					add_next_index_zval(&functions, &func_info);
				}
			}
			p++;
		}
	}

	if (IS_IN_RANGE(script->script.class_table.arData, total_size)) {
		p = (Bucket*)((char*)buf + (uintptr_t)script->script.class_table.arData);
		end = p + script->script.class_table.nNumUsed;
		while (p < end) {
			if (Z_TYPE(p->val) != IS_UNDEF) {
				zend_string *s = NULL;
				zend_class_entry *ce = NULL;
				if (Z_TYPE(p->val) == IS_PTR) {
					ce = (zend_class_entry*)((char*)buf + (uintptr_t)Z_PTR(p->val));
					if (IS_IN_BUF_RANGE(ce, buf, total_size)) {
						s = opkit_get_str_from_pool(buf, ce->name, info.mem_size, total_size);
					}
				}
				if (!s) {
					s = opkit_get_str_from_pool(buf, p->key, info.mem_size, total_size);
				}
				if (s) {
					zval class_info;
					array_init(&class_info);

					char *name = ZSTR_VAL(s);
					size_t len = ZSTR_LEN(s);
					/* Handle RTD classes */
					char *colon = memchr(name, ':', len);
					if (colon) {
						char *slash = colon;
						while (slash > name && slash[-1] != '/' && slash[-1] != '\\') {
							slash--;
						}
						len = colon - slash;
						name = slash;
					}
					add_assoc_stringl(&class_info, "name", name, len);

					if (ce && IS_IN_BUF_RANGE(ce, buf, total_size)) {
						zval methods;
						array_init(&methods);
						if (IS_IN_RANGE(ce->function_table.arData, total_size)) {
							Bucket *mp = (Bucket*)((char*)buf + (uintptr_t)ce->function_table.arData);
							Bucket *mend = mp + ce->function_table.nNumUsed;
							while (mp < mend && IS_IN_BUF_RANGE(mp, buf, total_size)) {
								if (Z_TYPE(mp->val) != IS_UNDEF) {
									zend_function *mf = (zend_function*)((char*)buf + (uintptr_t)Z_PTR(mp->val));
									if (IS_IN_BUF_RANGE(mf, buf, total_size)) {
										zend_string *ms = opkit_get_str_from_pool(buf, mf->common.function_name, info.mem_size, total_size);
										if (ms) {
											zval method_info;
											array_init(&method_info);
											add_assoc_stringl(&method_info, "name", ZSTR_VAL(ms), ZSTR_LEN(ms));

											smart_str sig = {0};
											opkit_get_function_signature(&sig, mf, buf, info.mem_size, total_size);
											if (sig.s) {
												add_assoc_stringl(&method_info, "signature", ZSTR_VAL(sig.s), ZSTR_LEN(sig.s));
												smart_str_free(&sig);
											}
											add_assoc_long(&method_info, "flags", mf->common.fn_flags);
											add_next_index_zval(&methods, &method_info);
										}
									}
								}
								mp++;
							}
						}
						add_assoc_zval(&class_info, "methods", &methods);

						zval props;
						array_init(&props);
						if (IS_IN_RANGE(ce->properties_info.arData, total_size)) {
							Bucket *pp = (Bucket*)((char*)buf + (uintptr_t)ce->properties_info.arData);
							Bucket *pend = pp + ce->properties_info.nNumUsed;
							while (pp < pend && IS_IN_BUF_RANGE(pp, buf, total_size)) {
								if (Z_TYPE(pp->val) != IS_UNDEF) {
									zend_property_info *prop = (zend_property_info*)((char*)buf + (uintptr_t)Z_PTR(pp->val));
									if (IS_IN_BUF_RANGE(prop, buf, total_size)) {
										zend_string *ps = opkit_get_str_from_pool(buf, prop->name, info.mem_size, total_size);
										if (ps) {
											const char *pname = ZSTR_VAL(ps);
											size_t plen = ZSTR_LEN(ps);
											if (plen > 0 && pname[0] == '\0') {
												const char *actual_name = (const char*)memrchr(pname, '\0', plen);
												if (actual_name && actual_name < pname + plen - 1) {
													pname = actual_name + 1;
													plen = strlen(pname);
												}
											}
											zval prop_info;
											array_init(&prop_info);
											add_assoc_stringl(&prop_info, "name", pname, plen);
											add_assoc_long(&prop_info, "flags", prop->flags);
											add_next_index_zval(&props, &prop_info);
										}
									}
								}
								pp++;
							}
						}
						add_assoc_zval(&class_info, "properties", &props);

						zval class_consts;
						array_init(&class_consts);
						if (IS_IN_RANGE(ce->constants_table.arData, total_size)) {
							Bucket *cp = (Bucket*)((char*)buf + (uintptr_t)ce->constants_table.arData);
							Bucket *cend = cp + ce->constants_table.nNumUsed;
							while (cp < cend && IS_IN_BUF_RANGE(cp, buf, total_size)) {
								if (Z_TYPE(cp->val) != IS_UNDEF) {
									zend_class_constant *c = (zend_class_constant*)((char*)buf + (uintptr_t)Z_PTR(cp->val));
									if (IS_IN_BUF_RANGE(c, buf, total_size)) {
										zend_string *cs = opkit_get_str_from_pool(buf, cp->key, info.mem_size, total_size);
										if (cs) {
											add_next_index_stringl(&class_consts, ZSTR_VAL(cs), ZSTR_LEN(cs));
										}
									}
								}
								cp++;
							}
						}
						add_assoc_zval(&class_info, "constants", &class_consts);
					}
					add_next_index_zval(&classes, &class_info);
				}
			}
			p++;
		}
	}

	add_assoc_zval(return_value, "functions", &functions);
	add_assoc_zval(return_value, "classes", &classes);

	/* Collect global constants from opkit_persistent_script */
	zval constants;
	array_init(&constants);
	opkit_persistent_script *op_script = (opkit_persistent_script *)script;
	if (IS_IN_RANGE(op_script->constants_table.arData, total_size)) {
		p = (Bucket*)((char*)buf + (uintptr_t)op_script->constants_table.arData);
		end = p + op_script->constants_table.nNumUsed;
		while (p < end) {
			if (Z_TYPE(p->val) != IS_UNDEF && Z_TYPE(p->val) == IS_PTR) {
				zend_constant *c = (zend_constant*)((char*)buf + (uintptr_t)Z_PTR(p->val));
				if (IS_IN_BUF_RANGE(c, buf, total_size)) {
					zend_string *s = opkit_get_str_from_pool(buf, c->name, info.mem_size, total_size);
					if (s) {
						zval const_info;
						array_init(&const_info);
						add_assoc_stringl(&const_info, "name", ZSTR_VAL(s), ZSTR_LEN(s));
						add_assoc_long(&const_info, "type", Z_TYPE(c->value));
						add_next_index_zval(&constants, &const_info);
					}
				}
			}
			p++;
		}
	}
	add_assoc_zval(return_value, "constants", &constants);

	efree(buf);
}

ZEND_FUNCTION(opkit_is_loaded)
{
	zend_string *filename;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(filename)
	ZEND_PARSE_PARAMETERS_END();

	zend_string *resolved = opkit_resolve_path(filename);
	if (!resolved) {
		RETURN_FALSE;
	}

	/* Also try with .phpc suffix applied (for users who pass the source .php path) */
	char *full_path = opkit_compile_get_phpc_file_path(NULL, resolved);
	zend_string *phpc_path = zend_string_init(full_path, strlen(full_path), 0);
	efree(full_path);

	opkit_script_node *node = loaded_scripts;
	while (node) {
		if (node->loaded_path) {
			if (zend_string_equals(resolved, node->loaded_path) ||
			    zend_string_equals(phpc_path, node->loaded_path)) {
				zend_string_release(phpc_path);
				zend_string_release(resolved);
				RETURN_TRUE;
			}
		}
		node = node->next;
	}

	zend_string_release(phpc_path);
	zend_string_release(resolved);
	RETURN_FALSE;
}

static zend_string *opkit_string_replace(zend_string *haystack, zend_string *needle, zend_string *replacement) {
	char *pos = strstr(ZSTR_VAL(haystack), ZSTR_VAL(needle));
	if (!pos) return NULL;

	smart_str res = {0};
	smart_str_appendl(&res, ZSTR_VAL(haystack), pos - ZSTR_VAL(haystack));
	smart_str_append(&res, replacement);
	smart_str_appends(&res, pos + ZSTR_LEN(needle));
	smart_str_0(&res);
	return res.s;
}

/* opkit_fix_op_array_filenames
 * Key function: Recursively fix filenames in OpArray and its Literals.
 * When the compiled .phpc runs in a Phar archive, this function replaces absolute paths with phar:// paths,
 * thereby fixing __FILE__, __DIR__, and include/require logic.
 */
static void opkit_fix_op_array_filenames(zend_op_array *op_array, zend_string *phar_prefix) {
	if (op_array->filename && strstr(ZSTR_VAL(op_array->filename), "phar://") != ZSTR_VAL(op_array->filename)) {
		zend_string *old_filename = op_array->filename;
		smart_str full_name = {0};
		smart_str_append(&full_name, phar_prefix);
		smart_str_appendc(&full_name, '/');
		smart_str_append(&full_name, old_filename);
		smart_str_0(&full_name);

		zend_string *new_filename = zend_new_interned_string(full_name.s);
		full_name.s = NULL;
		smart_str_free(&full_name);

		op_array->filename = new_filename;

		// Fix __FILE__ magic constant in literals (handles folded cases)
		if (op_array->literals) {
			for (int i = 0; i < op_array->last_literal; i++) {
				zval *zv = &op_array->literals[i];
				if (Z_TYPE_P(zv) == IS_STRING) {
					zend_string *s = Z_STR_P(zv);
					zend_string *replaced = opkit_string_replace(s, old_filename, new_filename);
					if (replaced) {
						// Replace the string in the zval. Since literals are in opkit persistent memory,
						// their refcount is not automatically managed by PHP.
						// We directly use zend_new_interned_string to ensure it survives until the end of the request.
						ZVAL_STR(zv, zend_new_interned_string(replaced));
					}
				}
			}
		}
	}
}

/* Helper function to create objects and initialize properties
 * This wraps zend_objects_new and adds property initialization
 */
static zend_object *opkit_create_object_with_props(zend_class_entry *ce) {
	zend_object *obj = zend_objects_new(ce);
	/* Initialize object properties from class default properties table */
	if (ce->default_properties_count) {
		zval *src = ce->default_properties_table;
		zval *dst = obj->properties_table;
		zval *end = src + ce->default_properties_count;
		do {
			ZVAL_COPY_PROP(dst, src);
			src++;
			dst++;
		} while (src != end);
	}
	return obj;
}

/* Resolve class dependencies - check if parent, interfaces and traits exist
 * Based on preload_resolve_deps() from ZendAccelerator.c
 */
static zend_result opkit_resolve_class_deps(opkit_link_error *error, const zend_class_entry *ce)
{
	memset(error, 0, sizeof(opkit_link_error));

	/* Skip if already linked */
	if (ce->ce_flags & ZEND_ACC_LINKED) {
		return SUCCESS;
	}

	/* Check parent class */
	if (ce->parent_name) {
		zend_string *key = zend_string_tolower(ce->parent_name);
		zend_class_entry *parent = zend_hash_find_ptr(EG(class_table), key);
		zend_string_release(key);
		if (!parent) {
			error->kind = "Unknown parent";
			error->name = ZSTR_VAL(ce->parent_name);
			return FAILURE;
		}
	}

	/* Check interfaces */
	if (ce->num_interfaces) {
		for (uint32_t i = 0; i < ce->num_interfaces; i++) {
			zend_class_entry *interface =
				zend_hash_find_ptr(EG(class_table), ce->interface_names[i].lc_name);
			if (!interface) {
				error->kind = "Unknown interface";
				error->name = ZSTR_VAL(ce->interface_names[i].name);
				return FAILURE;
			}
		}
	}

	/* Check traits */
	if (ce->num_traits) {
		for (uint32_t i = 0; i < ce->num_traits; i++) {
			zend_class_entry *trait =
				zend_hash_find_ptr(EG(class_table), ce->trait_names[i].lc_name);
			if (!trait) {
				error->kind = "Unknown trait";
				error->name = ZSTR_VAL(ce->trait_names[i].name);
				return FAILURE;
			}
		}
	}

	return SUCCESS;
}

/* Try to resolve class constants
 * Based on preload_try_resolve_constants() from ZendAccelerator.c
 */
static bool opkit_try_resolve_class_constants(zend_class_entry *ce)
{
	bool ok, changed, was_changed = false;
	zend_class_constant *c;
	zval *val;

	/* Skip traits - don't update trait constants in the same way */
	if (ce->ce_flags & ZEND_ACC_TRAIT) {
		return true;
	}

	/* Prevent error reporting during constant resolution */
	EG(exception) = (void*)(uintptr_t)-1;

	do {
		ok = true;
		changed = false;

		/* Resolve class constants */
		ZEND_HASH_MAP_FOREACH_PTR(&ce->constants_table, c) {
			val = &c->value;
			if (Z_TYPE_P(val) == IS_CONSTANT_AST) {
				if (EXPECTED(zval_update_constant_ex(val, c->ce) == SUCCESS)) {
					was_changed = changed = true;
				} else {
					ok = false;
				}
			}
		} ZEND_HASH_FOREACH_END();

		/* Resolve default properties */
		if (ce->default_properties_count) {
			bool resolved = true;
			for (uint32_t i = 0; i < ce->default_properties_count; i++) {
				val = &ce->default_properties_table[i];
				if (Z_TYPE_P(val) == IS_CONSTANT_AST) {
					zend_property_info *prop = ce->properties_info_table[i];
					if (UNEXPECTED(zval_update_constant_ex(val, prop->ce) != SUCCESS)) {
						resolved = ok = false;
					}
				}
			}
			if (resolved) {
				ce->ce_flags &= ~ZEND_ACC_HAS_AST_PROPERTIES;
			}
		}

		/* Resolve static members */
		if (ce->default_static_members_count) {
			uint32_t count = ce->parent
				? ce->default_static_members_count - ce->parent->default_static_members_count
				: ce->default_static_members_count;
			bool resolved = true;

			val = ce->default_static_members_table + ce->default_static_members_count - 1;
			while (count) {
				if (Z_TYPE_P(val) == IS_CONSTANT_AST) {
					if (UNEXPECTED(zval_update_constant_ex(val, ce) != SUCCESS)) {
						resolved = ok = false;
					}
				}
				val--;
				count--;
			}
			if (resolved) {
				ce->ce_flags &= ~ZEND_ACC_HAS_AST_STATICS;
			}
		}
	} while (changed && !ok);

	EG(exception) = NULL;
	CG(in_compilation) = false;

	if (ok) {
		ce->ce_flags |= ZEND_ACC_CONSTANTS_UPDATED;
	}

	return ok || was_changed;
}

/* Resolve all class constants after class linking
 * Based on the constants resolution loop in preload_link()
 */
static void opkit_resolve_all_class_constants(void)
{
	bool changed;
	zend_class_entry *ce;
	zval *zv;

	do {
		changed = false;

		ZEND_HASH_MAP_REVERSE_FOREACH_VAL(EG(class_table), zv) {
			ce = Z_PTR_P(zv);
			if (ce->type == ZEND_INTERNAL_CLASS) {
				break;
			}
			if ((ce->ce_flags & ZEND_ACC_LINKED) && !(ce->ce_flags & ZEND_ACC_CONSTANTS_UPDATED)) {
				if (!(ce->ce_flags & ZEND_ACC_TRAIT)) {
					CG(in_compilation) = true; /* prevent autoloading */
					if (opkit_try_resolve_class_constants(ce)) {
						changed = true;
					}
					CG(in_compilation) = false;
				}
			}
		} ZEND_HASH_FOREACH_END();
	} while (changed);
}

/* Main class linking function
 * Based on preload_link() from ZendAccelerator.c
 *
 * This implementation:
 * 1. Uses multi-pass loop to handle dependency chains
 * 2. Uses zend_do_link_class() for proper class linking
 * 3. Handles errors and rollbacks properly
 */
static void opkit_link_classes(void)
{
	zend_string *key;
	zval *zv;
	zend_class_entry *ce;
	bool changed;
	HashTable errors;

	zend_hash_init(&errors, 0, NULL, NULL, 0);

	/* First pass: Resolve class dependencies and link classes
	 * We loop multiple times to handle dependency chains (A extends B extends C)
	 */
	do {
		changed = false;

		ZEND_HASH_MAP_FOREACH_STR_KEY_VAL(EG(class_table), key, zv) {
			ce = Z_PTR_P(zv);

			/* Skip internal classes */
			if (ce->type == ZEND_INTERNAL_CLASS) {
				continue;
			}

			/* Skip if already linked or not a top-level/anonymous class */
			if (!(ce->ce_flags & (ZEND_ACC_TOP_LEVEL|ZEND_ACC_ANON_CLASS))
					|| (ce->ce_flags & ZEND_ACC_LINKED)) {
				continue;
			}

			/* Check if class name already exists (for non-anonymous classes) */
			zend_string *lcname = zend_string_tolower(ce->name);
			if (!(ce->ce_flags & ZEND_ACC_ANON_CLASS)) {
				/* Skip if already declared by another script */
				if (zend_hash_exists(EG(class_table), lcname)) {
					zend_class_entry *existing = zend_hash_find_ptr(EG(class_table), lcname);
					if (existing != ce) {
						zend_string_release(lcname);
						continue;
					}
				}
			}

			/* Resolve dependencies */
			opkit_link_error error_info;
			if (opkit_resolve_class_deps(&error_info, ce) == FAILURE) {
				zend_string_release(lcname);
				continue; /* Dependencies not ready, try again next iteration */
			}

			/* Update hash key to lowercase name for proper lookup */
			if (!zend_string_equals(key, lcname)) {
				zv = zend_hash_set_bucket_key(EG(class_table), (Bucket*)zv, lcname);
				if (!zv) {
					/* Key collision, skip this class */
					zend_string_release(lcname);
					continue;
				}
			}

			/* Prepare for class linking
			 * We temporarily set FILE_CACHED flag to force lazy loading behavior
			 * and CACHED flag to prevent freeing of interface names.
			 */
			void *checkpoint = zend_arena_checkpoint(CG(arena));
			zend_class_entry *orig_ce = ce;
			ce->ce_flags |= ZEND_ACC_FILE_CACHED|ZEND_ACC_CACHED;
			if (ce->parent_name) {
				zend_string_addref(ce->parent_name);
			}

			/* Set compilation context for inheritance errors */
			bool orig_in_compilation = CG(in_compilation);
			zend_string *orig_compiled_filename = CG(compiled_filename);
			CG(in_compilation) = true;
			CG(compiled_filename) = ce->info.user.filename;
			CG(zend_lineno) = ce->info.user.line_start;

			/* Attempt to link the class using zend_do_link_class */
			zend_try {
				ce = zend_do_link_class(ce, NULL, lcname);
				if (ce) {
					/* Success - update the pointer and clean up flags */
					Z_CE_P(zv) = ce;
					ce->ce_flags &= ~(ZEND_ACC_FILE_CACHED|ZEND_ACC_CACHED);
					ce->ce_flags &= ~ZEND_ACC_IMMUTABLE;
#if PHP_VERSION_ID >= 80300
					ce->default_object_handlers = &std_object_handlers;
#endif
					changed = true;
				} else {
					/* Linking failed but no exception - restore flags */
					orig_ce->ce_flags &= ~(ZEND_ACC_FILE_CACHED|ZEND_ACC_CACHED);
					zend_arena_release(&CG(arena), checkpoint);
				}
			} zend_catch {
				/* Linking failed with exception - restore original class */
				orig_ce->ce_flags &= ~(ZEND_ACC_FILE_CACHED|ZEND_ACC_CACHED);
				zv = zend_hash_set_bucket_key(EG(class_table), (Bucket*)zv, key);
				Z_CE_P(zv) = orig_ce;
				zend_arena_release(&CG(arena), checkpoint);

				/* Clear variance obligations */
				if (CG(delayed_variance_obligations)) {
					zend_hash_index_del(
						CG(delayed_variance_obligations), (uintptr_t) Z_CE_P(zv));
				}
			} zend_end_try();

			/* Restore compilation context */
			CG(in_compilation) = orig_in_compilation;
			CG(compiled_filename) = orig_compiled_filename;
			CG(zend_lineno) = 0;

			zend_string_release(lcname);
		} ZEND_HASH_FOREACH_END();
	} while (changed);

	/* Warn for classes that could not be linked */
	ZEND_HASH_MAP_FOREACH_STR_KEY_VAL(EG(class_table), key, zv) {
		ce = Z_PTR_P(zv);
		if (ce->type == ZEND_INTERNAL_CLASS) {
			continue;
		}
		if ((ce->ce_flags & (ZEND_ACC_TOP_LEVEL|ZEND_ACC_ANON_CLASS))
				&& !(ce->ce_flags & ZEND_ACC_LINKED)) {
			opkit_link_error error;
			if (opkit_resolve_class_deps(&error, ce) == SUCCESS) {
				/* Dependencies exist but linking failed - warn */
				php_error_docref(NULL, E_WARNING,
					"Can't load unlinked class %s: %s %s",
					ZSTR_VAL(ce->name), error.kind, error.name);
			}
		}
	} ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&errors);
}

// Start and execute the entry function
ZEND_FUNCTION(opkit_boot) {
	zend_fcall_info fci = empty_fcall_info;
	zend_fcall_info_cache fci_cache = empty_fcall_info_cache;
	zval *args = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_OR_NULL(fci, fci_cache)
		Z_PARAM_ARRAY(args)
	ZEND_PARSE_PARAMETERS_END();

	if (!ZEND_FCI_INITIALIZED(fci)) {
		// If not passed via parameters, try to find a function named "main"
		zend_function *main_func = zend_hash_str_find_ptr(EG(function_table), "main", sizeof("main") - 1);
		if (main_func) {
			fci.size = sizeof(zend_fcall_info);
			fci_cache.function_handler = main_func;
		}
	}

	if (!loaded_scripts) {
		zend_throw_exception(NULL, "No script loaded for opkit_boot", 0);
		return;
	}

	zend_string *phar_prefix = NULL;
	opkit_script_node *node;
	zend_string *cur_file = zend_get_executed_filename_ex();
	if (cur_file && strstr(ZSTR_VAL(cur_file), "phar://") == ZSTR_VAL(cur_file)) {
		char *p = strstr(ZSTR_VAL(cur_file), ".phar");
		if (p) {
			phar_prefix = zend_string_init(ZSTR_VAL(cur_file), (p - ZSTR_VAL(cur_file)) + 5, 0);
		}
	}

	// Phase 1: Register all classes, functions and constants
	dtor_func_t orig_class_dtor = EG(class_table)->pDestructor;
	EG(class_table)->pDestructor = NULL;
	dtor_func_t orig_func_dtor = EG(function_table)->pDestructor;
	EG(function_table)->pDestructor = NULL;
	dtor_func_t orig_const_dtor = EG(zend_constants)->pDestructor;
	EG(zend_constants)->pDestructor = NULL;

	node = loaded_scripts;
	while (node) {
		zend_persistent_script *script = node->script;
		opkit_persistent_script *op_script = (opkit_persistent_script *)script;
		zend_class_entry *ce;
		zend_function *f;

		if (phar_prefix) {
			opkit_fix_op_array_filenames(&script->script.main_op_array, phar_prefix);
		}

		/* 1. Register classes to class_table (pre-linking registration)
		 * We register all classes first without linking them.
		 * The linking will be done in a separate pass after all classes are registered.
		 */
		zend_string *key;
		ZEND_HASH_FOREACH_STR_KEY_PTR(&script->script.class_table, key, ce) {
			if (phar_prefix) {
				ce->info.user.filename = script->script.main_op_array.filename;
			}
				// Register with original key (including RTD Keys)
			if (key) {
				zend_hash_update_ptr(EG(class_table), key, ce);
			} else {
				// For classes without explicit keys, use ce->name
				zend_hash_update_ptr(EG(class_table), ce->name, ce);
			}

			// Clear ZEND_ACC_LINKED - will be set by opkit_link_classes()
			// ce->ce_flags &= ~ZEND_ACC_LINKED;
			ce->ce_flags &= ~ZEND_ACC_IMMUTABLE;

			// Fix method filenames and immutability flags
			zend_function *m;
			ZEND_HASH_FOREACH_PTR(&ce->function_table, m) {
				if (m->type == ZEND_USER_FUNCTION) {
					if (phar_prefix) {
						opkit_fix_op_array_filenames(&m->op_array, phar_prefix);
					}
					m->op_array.fn_flags &= ~ZEND_ACC_IMMUTABLE;
				}
			} ZEND_HASH_FOREACH_END();

#if PHP_VERSION_ID >= 80400
			// Fix Property Hooks immutability flags
			zend_property_info *prop_info;
			ZEND_HASH_FOREACH_PTR(&ce->properties_info, prop_info) {
				if (prop_info->hooks) {
					for (uint32_t i = 0; i < ZEND_PROPERTY_HOOK_COUNT; i++) {
						zend_function *hook = prop_info->hooks[i];
						if (hook && hook->type == ZEND_USER_FUNCTION) {
							if (phar_prefix) {
								opkit_fix_op_array_filenames(&hook->op_array, phar_prefix);
							}
							hook->op_array.fn_flags &= ~ZEND_ACC_IMMUTABLE;
						}
					}
				}
			} ZEND_HASH_FOREACH_END();
#endif
		} ZEND_HASH_FOREACH_END();

		// 2. Register functions
		ZEND_HASH_FOREACH_PTR(&script->script.function_table, f) {
			if (f->type != ZEND_USER_FUNCTION) continue;

			if (phar_prefix) {
				opkit_fix_op_array_filenames(&f->op_array, phar_prefix);
			}

			zend_string *lcname = zend_string_tolower(f->common.function_name);
			zend_hash_update_ptr(EG(function_table), lcname, f);
			zend_string_release(lcname);

			f->op_array.fn_flags &= ~ZEND_ACC_IMMUTABLE;
		} ZEND_HASH_FOREACH_END();

		// 3. Register constants
		zend_constant *zc;
		ZEND_HASH_FOREACH_PTR(&op_script->constants_table, zc) {
			if (zc->name) {
				zend_hash_update_ptr(EG(zend_constants), zc->name, zc);
			}
		} ZEND_HASH_FOREACH_END();

 		node = node->next;
	}

	/* Phase 1.5: Link classes
	 * After all classes are registered, we perform proper class linking.
	 * This resolves inheritance dependencies, interface implementations,
	 * trait usage, and validates class hierarchies.
	 *
	 * This is based on preload_link() from Zend OPcache.
	 */
	opkit_link_classes();

	/* Phase 1.6: Resolve class constants
	 * After class linking, resolve constants that may depend on other classes.
	 */
	opkit_resolve_all_class_constants();

	// Phase 2: Execute all scripts (after class linking is complete)
	node = loaded_scripts;
	while (node) {
		zend_persistent_script *script = node->script;
		opkit_persistent_script *op_script = (opkit_persistent_script *)script;

		// NOP out declarations in main_op_array to avoid re-declaration errors during execution
		zend_op *opline = script->script.main_op_array.opcodes;
		zend_op *opline_end = opline + script->script.main_op_array.last;
		while (opline < opline_end) {
			if (opline->opcode == ZEND_DECLARE_FUNCTION ||
				opline->opcode == ZEND_DECLARE_CLASS ||
				opline->opcode == ZEND_DECLARE_CLASS_DELAYED ||
				opline->opcode == ZEND_DECLARE_CONST) {
				opline->opcode = ZEND_NOP;
				zend_vm_set_opcode_handler(opline);
			}
			opline++;
		}

		if (script->early_bindings) {
			zend_accel_finalize_delayed_early_binding_list(script);
			zend_accel_do_delayed_early_binding(script, &script->script.main_op_array);
		}

		// Execute the script's global code (main_op_array)
		// This will execute define(), global const, and any top-level logic
		zend_op_array *main_op_array = &script->script.main_op_array;
		zval retval;
		zend_execute(main_op_array, &retval);
		zval_ptr_dtor(&retval);

		// Mark as executed so runtime cache cleanup will occur
		node->executed = true;

		node = node->next;
	}

	if (phar_prefix) {
		zend_string_release(phar_prefix);
	}

	EG(class_table)->pDestructor = orig_class_dtor;
	EG(function_table)->pDestructor = orig_func_dtor;
	EG(zend_constants)->pDestructor = orig_const_dtor;

	// Execute entry function
	if (ZEND_FCI_INITIALIZED(fci)) {
		zval main_retval;
		if (args) {
			zend_fcall_info_args(&fci, args);
		}
		fci.retval = &main_retval;
		if (zend_call_function(&fci, &fci_cache) == SUCCESS) {
			if (args) {
				zend_fcall_info_args_clear(&fci, 1);
			}
			if (Z_TYPE(main_retval) == IS_LONG) {
				RETVAL_LONG(Z_LVAL(main_retval));
			} else {
				RETVAL_LONG(0);
			}
			zval_ptr_dtor(&main_retval);
		} else {
			if (args) {
				zend_fcall_info_args_clear(&fci, 1);
			}
			zend_throw_exception(NULL, "Failed to call entry function", 0);
		}
	} else {
		// Find and execute default main()
		// Note: OpKit encourages using main() as the entry function in phpc-compiled scripts,
		// this ensures all dependent classes and functions are registered before main execution.
		zend_function *main_func = zend_hash_str_find_ptr(EG(function_table), "main", sizeof("main") - 1);
		if (main_func) {
			zend_fcall_info fci_main = empty_fcall_info;
			zend_fcall_info_cache fci_cache_main = empty_fcall_info_cache;
			zval main_retval;
			fci_main.size = sizeof(zend_fcall_info);
			fci_main.retval = &main_retval;
			fci_cache_main.function_handler = main_func;
			if (zend_call_function(&fci_main, &fci_cache_main) == SUCCESS) {
				if (Z_TYPE(main_retval) == IS_LONG) {
					RETVAL_LONG(Z_LVAL(main_retval));
				} else {
					RETVAL_LONG(0);
				}
				zval_ptr_dtor(&main_retval);
			} else {
				zend_throw_exception(NULL, "Failed to call main function", 0);
			}
		} else {
			zend_throw_exception(NULL, "Entry point \"main\" not found", 0);
		}
	}
}


static void opkit_collect_phpc_files(php_stream *stream, char *dir_path, size_t base_dir_len) {
	php_stream *dir = php_stream_opendir(dir_path, REPORT_ERRORS, NULL);
	if (!dir) return;

	php_stream_dirent entry;
	while (php_stream_readdir(dir, &entry)) {
		if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0) continue;

		char full_path[MAXPATHLEN];
		snprintf(full_path, MAXPATHLEN, "%s/%s", dir_path, entry.d_name);

		struct stat st;
		if (stat(full_path, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				opkit_collect_phpc_files(stream, full_path, base_dir_len);
			} else if (S_ISREG(st.st_mode)) {
				const char *ext = strrchr(entry.d_name, '.');
				if (ext && strcmp(ext, ".phpc") == 0) {
					const char *rel_path = full_path + base_dir_len;
					while (rel_path[0] == '/' || rel_path[0] == '\\') rel_path++;

					php_stream_printf(stream, "    __DIR__ . '/%s',\n", rel_path);
				}
			}
		}
	}
	php_stream_closedir(dir);
}

ZEND_FUNCTION(opkit_gen_entry_file) {
	zend_string *output_path;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &output_path) == FAILURE) {
		RETURN_THROWS();
	}

	zend_string *resolved_output_path = opkit_resolve_path(output_path);

	php_stream *stream = php_stream_open_wrapper(ZSTR_VAL(resolved_output_path), "w", REPORT_ERRORS, NULL);
	if (!stream) {
		zend_string_release(resolved_output_path);
		RETURN_FALSE;
	}

	char *dir = estrndup(ZSTR_VAL(resolved_output_path), ZSTR_LEN(resolved_output_path));
	char *slash = strrchr(dir, '/');
#ifdef PHP_WIN32
	if (!slash) slash = strrchr(dir, '\\');
#endif
	if (slash) {
		if (slash == dir) {
			*(slash + 1) = '\0';
		} else {
			*slash = '\0';
		}
	} else {
		efree(dir);
		dir = estrdup(".");
	}

	php_stream_printf(stream, "<?php\n"
		"\n"
		"if (!extension_loaded('opkit')) {\n"
		"    if (!@dl('opkit.so')) {\n"
		"        trigger_error('OpKit extension not loaded', E_USER_ERROR);\n"
		"    }\n"
		"}\n"
		"\n"
		"opkit_load_multi([\n");

	size_t base_dir_len = strlen(dir);
	opkit_collect_phpc_files(stream, dir, base_dir_len);

	php_stream_printf(stream, "]);\n"
		"\n"
		"exit(opkit_boot());\n");

	php_stream_close(stream);
	efree(dir);
	zend_string_release(resolved_output_path);

	RETURN_TRUE;
}


int start_opkit_module(void)
{
	return zend_startup_module(&opkit_module_entry);
}

ZEND_INI_BEGIN()
ZEND_INI_END()

static ZEND_MINIT_FUNCTION(opkit)
{
	(void)type; /* keep the compiler happy */

	if (zend_get_extension("Zend OPcache")) {
		zend_error(E_CORE_ERROR, OPKIT_EXTENSION_NAME " is incompatible with Zend OPcache");
		return FAILURE;
	}

	REGISTER_INI_ENTRIES();

	return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(opkit)
{
	(void)type; /* keep the compiler happy */
	opkit_reset_script();
	return SUCCESS;
}

static ZEND_MSHUTDOWN_FUNCTION(opkit)
{
	(void)type; /* keep the compiler happy */

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

static PHP_MINFO_FUNCTION(opkit)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "OpKit support", "enabled");
	php_info_print_table_row(2, "Version", OPKIT_EXTENSION_VERSION);
	php_info_print_table_row(2, "Author", "Eno-CN <Eno_CN@qq.com>");
	php_info_print_table_row(2, "Credits", "Heavily based on Zend OPcache");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

static const zend_function_entry opkit_functions[] = {
	ZEND_FE(opkit_compile_file, arginfo_opkit_compile_file)
	ZEND_FE(opkit_compile_dir, arginfo_opkit_compile_dir)
	ZEND_FE(opkit_load, arginfo_opkit_load)
	ZEND_FE(opkit_load_multi, arginfo_opkit_load_multi)
	ZEND_FE(opkit_boot, arginfo_opkit_boot)
	ZEND_FE(opkit_gen_entry_file, arginfo_opkit_gen_entry_file)
	ZEND_FE(opkit_get_info, arginfo_opkit_get_info)
	ZEND_FE(opkit_is_loaded, arginfo_opkit_is_loaded)
	ZEND_FE_END
};

zend_module_entry opkit_module_entry = {
	STANDARD_MODULE_HEADER,
	OPKIT_EXTENSION_NAME,
	opkit_functions,
	ZEND_MINIT(opkit),
	ZEND_MSHUTDOWN(opkit),
	NULL,
	PHP_RSHUTDOWN(opkit),
	ZEND_MINFO(opkit),
	OPKIT_EXTENSION_VERSION,
	NO_MODULE_GLOBALS,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
