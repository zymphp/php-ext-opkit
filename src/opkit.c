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
#include "opkit_wrapper.h"
#include "ext/standard/php_var.h"
#include "Zend/zend_extensions.h"
#include "Zend/zend_smart_str.h"

zend_accel_globals accel_globals;

ZEND_EXTENSION();

static int opkit_startup(zend_extension *extension) {
	if (start_opkit_module() == FAILURE) {
		zend_error(E_WARNING, OPKIT_EXTENSION_NAME ": module registration failed!");
		return FAILURE;
	}

	/* Prevent unloading */
	extension->handle = 0;

	return SUCCESS;
}

void shutdown_opkit_extension(void) {

}

static void opkit_zend_extension_activate(void) {
	CG(compiler_options) |= ZEND_COMPILE_EXTENDED_INFO;
}

static void opkit_zend_extension_deactivate(void) {
	CG(compiler_options) &= ~ZEND_COMPILE_EXTENDED_INFO;
}

#if 0
static void opkit_zend_extension_message_handler(int code, void *ext) {
	php_printf("We just detected that zend_extension '%s' is trying to load\n", ((zend_extension *)ext)->name);
}

static void opkit_zend_extension_op_array_handler(zend_op_array *op_array) {
	smart_str out = {0};

	smart_str_appends(&out, "We just compiled ");

	if (op_array->function_name) {
		uint32_t i, num_args = op_array->num_args;

		if (op_array->fn_flags & ZEND_ACC_CLOSURE) {
			smart_str_appends(&out, "a closure ");
		} else {
			smart_str_appends(&out, "function ");
			smart_str_append(&out, op_array->function_name);
		}
		smart_str_appendc(&out, '(');

		/* The variadic arg is not declared as an arg internally */
		if (op_array->fn_flags & ZEND_ACC_VARIADIC) {
			num_args++;
		}
		for (i=0; i<num_args; i++) {
			zend_arg_info arg = op_array->arg_info[i];

			if (ZEND_TYPE_IS_SET(arg.type)) {
				smart_str_append(&out, zend_type_to_string(arg.type));
				smart_str_appendc(&out, ' ');
			}

			if (ZEND_ARG_SEND_MODE(&arg) == ZEND_SEND_BY_REF) {
				smart_str_appendc(&out, '&');
			}

			if (ZEND_ARG_IS_VARIADIC(&arg)) {
				smart_str_appends(&out, "...");
			}

			smart_str_appendc(&out, '$');
			smart_str_append(&out, arg.name);
			if (i != num_args - 1) {
				smart_str_appends(&out, ", ");
			}
		}

		smart_str_appends(&out, ") in file ");
		smart_str_append(&out, op_array->filename);
		smart_str_appends(&out, " between line ");
		smart_str_append_unsigned(&out, op_array->line_start);
		smart_str_appends(&out, " and line ");
		smart_str_append_unsigned(&out, op_array->line_end);
	} else {
		smart_str_appends(&out, "the file ");
		smart_str_append(&out, op_array->filename);
	}

	smart_str_0(&out);
	php_printf("%s\n", ZSTR_VAL(out.s));
	smart_str_free(&out);
}

static void opkit_zend_extension_fcall_begin_handler(zend_execute_data *execute_data) {
	if (!execute_data->call) {
		/* Fetch the next OPline. We use pointer arithmetic for that */
		zend_op n = execute_data->func->op_array.opcodes[(execute_data->opline - execute_data->func->op_array.opcodes) + 1];
		if (n.extended_value == ZEND_EVAL) {
			php_printf("Beginning of a code eval() in %s:%u", ZSTR_VAL(execute_data->func->op_array.filename), n.lineno);
		} else {
			/* The file to be include()ed is stored into the operand 1 of the OPLine */
			zend_string *file = execute_data->func->op_array.filename;
			php_printf("Beginning of an include of file '%s'", ZSTR_VAL(file));
			zend_string_release(file);
		}
	} else if (execute_data->call->func->common.fn_flags & ZEND_ACC_STATIC) {
		php_printf("Beginning of a new static method call : '%s::%s'",
					ZSTR_VAL(Z_CE(execute_data->call->This)->name),
					ZSTR_VAL(execute_data->call->func->common.function_name));
	} else if (Z_TYPE(execute_data->call->This) == IS_OBJECT) {
		php_printf("Beginning of a new method call : %s->%s",
					ZSTR_VAL(Z_OBJCE(execute_data->call->This)->name),
					execute_data->call->func->common.function_name == NULL ? "" : ZSTR_VAL(execute_data->call->func->common.function_name));
	} else if (execute_data->call->func->common.function_name) {
		php_printf("Beginning of a new function call : %s", ZSTR_VAL(execute_data->call->func->common.function_name));
	}

	PHPWRITE("\n", 1);
}
#endif

ZEND_EXT_API zend_extension zend_extension_entry = {
 OPKIT_EXTENSION_NAME,
 OPKIT_EXTENSION_VERSION,
 "Eno-CN <Eno_CN@qq.com>",
	"",
	"",
	opkit_startup,                             /* startup() : module startup */
	NULL,                            /* shutdown() : module shutdown */
	opkit_zend_extension_activate,        /* activate() : request startup */
	opkit_zend_extension_deactivate,      /* deactivate() : request shutdown */
	NULL, /* opkit_zend_extension_message_handler, */

	NULL, /* opkit_zend_extension_op_array_handler, */
	NULL,                                      /* VM statement_handler() */
	NULL, /* opkit_zend_extension_fcall_begin_handler, */
	NULL,                                       /* VM fcall_end_handler() */
	NULL,                                          /* compiler op_array_ctor() */
	NULL,                                         /* compiler op_array_dtor() */
	STANDARD_ZEND_EXTENSION_PROPERTIES             /* Structure-ending macro */
};


