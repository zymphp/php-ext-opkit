/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: dfb87732f1c6ba8b62c994c24cbbf557093b74e3 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_compile_file, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, output_path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, filename, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_compile_dir, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, output_path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, dir, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_load, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, filename, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_load_multi, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, filenames, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_boot, 0, 0, IS_LONG, 0)
	ZEND_ARG_TYPE_MASK(0, entry, MAY_BE_CALLABLE|MAY_BE_STRING|MAY_BE_NULL, "\"main\"")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, args, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_gen_entry_file, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, output_path, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_get_info, 0, 1, IS_ARRAY, 1)
	ZEND_ARG_TYPE_INFO(0, filename, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_opkit_is_loaded arginfo_opkit_load

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_shm_reset, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_opkit_shm_stat, 0, 0, IS_ARRAY, 1)
ZEND_END_ARG_INFO()
