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


#ifndef OPKIT_MODULE_H
#define OPKIT_MODULE_H

#include "opkit_wrapper.h"

#define phpext_opkit_ptr &opkit_module_entry
extern zend_module_entry opkit_module_entry;

BEGIN_EXTERN_C()
int start_opkit_module(void);
void opkit_clean_script_memory(void *mem, size_t size);
void opkit_keep_memory(zend_persistent_script *script, void *mem_to_free);
END_EXTERN_C()

#endif //OPKIT_MODULE_H
