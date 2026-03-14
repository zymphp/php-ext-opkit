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


#ifndef OPKIT_COMPILE_H
#define OPKIT_COMPILE_H

#include "opkit_wrapper.h"

BEGIN_EXTERN_C()
int opkit_compile_script_store(zend_string *output_path, zend_persistent_script *script, zend_string *base_path);
char *opkit_compile_get_phpc_file_path(zend_string *output_path, zend_string *rel_path);
zend_persistent_script *opkit_compile_file(zend_file_handle *file_handle, int type, zend_op_array **op_array_p);
zend_persistent_script *opkit_compile_script_load(zend_string *filename);
END_EXTERN_C()

#endif //OPKIT_COMPILE_H
