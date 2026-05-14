<?php

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

/** Compile a PHP file into an OpKit Opcode file */
function opkit_compile_file(string $output_path, string $filename, ?string $base_path = null): bool {}

/** Compile an entire directory into OpKit Opcode files */
function opkit_compile_dir(string $output_path, string $dir): bool {}

/** Load an OpKit Opcode file */
function opkit_load(string $filename): bool {}

/** Load multiple OpKit Opcode files from an array */
function opkit_load_multi(array $filenames): void {}

/** Boot the OpKit and execute the entry point */
function opkit_boot(callable|string|null $entry = "main", array $args = []): int {}

/** Generate a bootstrap entry Opcode file */
function opkit_gen_entry_file(string $output_path): bool {}

/** Get information about an OpKit Opcode file */
function opkit_get_info(string $filename): ?array {}

/** Check if an OpKit Opcode file is already loaded */
function opkit_is_loaded(string $filename): bool {}

/** Reset shared memory allocator, clearing all cached scripts */
function opkit_shm_reset(): bool {}

/** Get shared memory statistics */
function opkit_shm_stat(): ?array {}
