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

#ifndef OPKIT_H
#define OPKIT_H

#include "php.h"

#if PHP_VERSION_ID < 80200 || PHP_VERSION_ID >= 80400
# if PHP_VERSION_ID < 80200
#  error "OpKit extension requires PHP 8.2 or newer"
# else
#  error "OpKit extension does not yet support PHP 8.4 or 8.5 (support is planned)"
# endif
#endif

#define OPKIT_EXTENSION_NAME	"OPkit"
#define OPKIT_EXTENSION_VERSION	"0.0.1-dev"

#ifndef ZEND_EXT_API
# if defined(__GNUC__) && __GNUC__ >= 4
#  define ZEND_EXT_API __attribute__ ((visibility("default")))
# else
#  define ZEND_EXT_API
# endif
#endif

#endif //OPKIT_H
