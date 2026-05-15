# AGENTS.md — OpKit

OpKit is a PHP Zend Extension for offline Opcode pre-compilation (`.phpc` binary files).

## Build

```bash
# Use the matching phpize/php-config from php-src/. Example for PHP 8.5:
php-src/php-8.5.4/scripts/phpize && \
./configure --with-php-config=php-src/php-8.5.4/scripts/php-config && \
make
```

`config.m4` requires PHP 8.2–8.5.
The extension is always shared; `--enable-debug` is supported for ZEND_DEBUG builds.

## Test

```bash
# Run directly with the PHP test runner:
php-src/php-8.5.4/sapi/cli/php -d zend_extension=$(pwd)/modules/opkit.so \
  php-src/php-8.5.4/run-tests.php -d zend_extension=$(pwd)/modules/opkit.so tests/
```

Tests use `.phpt` format. Check failures in `tests/*.diff` and `tests/*.out`.
`ext/opkit/tests/` is empty; real tests are in root `tests/`.

Test results (2026-05-16): **0 failing across PHP 8.2/8.3/8.4/8.5** (100% of non-skipped).

| PHP | Pass | Skip | Fail | Rate |
|-----|------|------|------|------|
| 8.2 | 29 | 5 | 0 | 100% |
| 8.3 | 29 | 5 | 0 | 100% |
| 8.4 | 30 | 4 | 0 | 100% |
| 8.5 | 31 | 3 | 0 | 100% |

Skipped: `05_triple_des.phpt` (requires openssl), `18_property_hooks.phpt` (PHP 8.4+), `24_php85_fcc_const.phpt` (PHP 8.5+), `27_fork_shm.phpt`/`29_shm_reset_fork.phpt` (requires pcntl).

## Architecture Limitation

PHP's compiler arena (`ast_arena`) is destroyed by `zend_compile()` before OpKit's persistence runs. To prevent dangling arena pointers, `opkit_compile_file()` pre-resolves all `IS_CONSTANT_AST` values in class properties, class constants, and op_array literals by calling `opkit_update_constant_safe()` after registering file-level constants in `EG(zend_constants)`. This converts constant references to their resolved values before the persist phase, eliminating both stale AST pointer access and memory leaks.

For `ZEND_AST_CONST_ENUM_INIT` (enum cases), `zval_update_constant_ex()` is unsafe during compilation because the class may not be fully linked yet, causing `zend_lookup_class()` to return NULL and trigger a SIGSEGV in `zend_enum_new()`. Instead, `opkit_update_constant_safe()` copies the AST from the compiler arena to the heap via `opkit_copy_ast_ref()`, allowing the persist phase to serialize it safely. At runtime, when the class is linked, PHP resolves the AST normally. Heap-allocated AST refs are tracked in `opkit_ast_ref_list` and freed in bulk after persistence (or on failure).

After `zend_accel_script_persist()`, `zend_persist_op_array_ex` frees opcodes/arg_info/etc via `_opkit_shared_memdup_put_free_*()`, but leaves `dynamic_func_defs`, `static_variables` HashTable structure, and heap AST refs behind. OpKit collects all op_array pointers before persistence (`opkit_collect_op_arrays`) and cleans up these remaining resources afterward (`opkit_destroy_op_array_safe` + `opkit_free_ast_ref_list`), preventing memory leaks in large codebases.

Remaining edge cases:
- Class constant array keys using `self::CONST` where the class isn't fully linked at resolve time
- Constants referencing other constants from a different file (not yet loaded)

OpKit previously used `ZEND_COMPILE_NO_CONSTANT_SUBSTITUTION` (matching OPcache) but this prevented PHP's `pass_two()` from resolving these ASTs at compile time. Removing this flag allows most constant expressions to resolve correctly, but some edge cases remain.

## Cross-file Class Dependencies

When compiling multiple files in batch (`phpc -s src/ -o dist/`), each file is compiled independently. Classes from previously compiled files are removed from `CG(class_table)` by `zend_accel_move_user_classes()`, so subsequent files cannot find them. The phpc tool solves this by pre-scanning all source files, building a FQCN-to-path map, and registering `spl_autoload_register()`. When the compiler encounters an unknown class, the autoloader `require_once`s the dependency file, registering its classes in `CG(class_table)` before the main compilation continues.

Two new PHP functions support cleanup after batch compilation:
- `opkit_globals_mark()` — snapshot global table counts before batch
- `opkit_globals_cleanup()` — purge autoloaded entries after batch

## Loading the Extension

- **Must** be loaded as `zend_extension=opkit.so` — NOT `extension=opkit.so`.
- **All PHP versions (8.2–8.5)**: Coexists with OPcache via deep integration. OpKit temporarily restores the original `compile_file` during compilation, then restores OPcache's hook.
- Set `phar.readonly=Off` when creating Phar archives.
- A pre-built php-dev.ini exists at `tmp/php-dev.ini`.

## Run Locally

```bash
php-src/php-8.5.4/sapi/cli/php -d zend_extension=$(pwd)/modules/opkit.so ./bin/phpc --help
# Or use tmp/php-dev.ini:
php-src/php-8.5.4/sapi/cli/php -c tmp/php-dev.ini ./bin/phpc -s src/ -o dist/
```

## Source Layout

| Path | Role |
|------|------|
| `src/*.c` / `src/*.h` | C extension source |
| `src/opkit.c` | Extension entry point + internal module structs |
| `src/opkit_module.c` | PHP API functions (MINIT/MSHUTDOWN/RINIT/RSHUTDOWN) |
| `src/opkit_compile.c` | Compile + file I/O + serialize/deserialize |
| `src/opkit_zend_persist.c` | Copy compiled data to persistent memory |
| `src/opkit_zend_persist_calc.c` | Calculate memory sizes for four partitions |
| `src/opkit_util_funcs.c` | Hashtable persistence, checksum, script loading |
| `src/opkit_wrapper.h` | Compatibility macros (OPcache structure reuse) |
| `src/opkit.stub.php` | PHP API signature definitions (source of truth for arginfo) |
| `src/opkit_arginfo.h` | **Generated** — do not edit directly (regen with `make`) |
| `bin/phpc` | CLI build tool (requires extension loaded) |
| `composer/src/` | Composer plugin (auto-compile on install/update) |
| `stubs/opkit.php` | IDE stubs for C extension functions (autoloaded via Composer) |
| `docs/` | ARCHITECTURE.md, PHPC_FILE_FORMAT.md, COMPILATION_PROCESS.md |

`ext/opkit/` is a legacy layout remnant. The active code is under root `src/` and `tests/`.

## Adding/Changing PHP API Functions

1. Edit `src/opkit.stub.php` — this is the source of truth.
2. Run `make` to regenerate `src/opkit_arginfo.h` (calls `build/gen_stub.php`).
3. Implement in `src/opkit_module.c`.
4. Add a `.phpt` test in `tests/`.

## Memory Management

Four shadow partitions, sized with macros then allocated as one block via `ZCG(mem)`:

- `ADD_SIZE_MD(s)` — Metadata (headers, hashtables, class/function metadata)
- `ADD_SIZE_CD(s)` — Code (opcodes, literals)
- `ADD_SIZE_DT(s)` — Data (constants, strings, property defaults)
- `ADD_SIZE_MS(s)` — Misc (alignment, buffers)

## Conventions

- C version guards: `#if PHP_VERSION_ID >= 80400` / `80300` / else (8.2).
- `doc_comment` was removed from `zend_property_info` in PHP 8.4, but still exists in `zend_op_array`, `zend_class_constant`, and `zend_class_entry` (moved out of `info.user` union in 8.4+).
- Property hooks (`hooks[i]`): save original pointer before `SERIALIZE_PTR`, use saved pointer to serialize each hook. On persist, set `hook->prop_info = copy` before calling `zend_persist_op_array_ex`.
- `/tmp/` is gitignored — use it for throwaway test scripts.
- `AI_DEV_ENV.md` is gitignored (machine-specific paths). `AI_GUIDELINES.md` contains version-agnostic conventions (now merged into this file).

## Development Pitfalls

- **Symbol Registration**: When registering classes/functions in `opkit_boot`, existing symbols are replaced.
- **Double-Free Risks**: During `RSHUTDOWN`, ensure that `opkit_script_node` and associated memory are cleaned up exactly once.
- **Phar Paths**: OpKit automatically fixes relative paths (`__FILE__`, `__DIR__`) within Phar archives during `opkit_boot`.
- **Constants**: Dynamic `define()` constants are handled by executing the `main_op_array` of the loaded script inside `opkit_boot`.
- **Static Analysis**: `phpc --stubs <dir>` generates PHP stubs from binary `.phpc` files for IDE support. `stubs/opkit.php` provides IDE definitions for the extension's C functions (autoloaded via Composer `autoload.files`).

## Incremental Builds

`phpc` skips recompilation when target exists and source mtime ≤ target mtime AND system_id matches. System ID encodes PHP version + arch + compile options. Force rebuild with `-f`.

## Known Issues

### PHP 8.2 Persistent String Conflict with swoole/curl

When both `swoole` and `curl` extensions are loaded alongside OpKit, a `munmap_chunk(): invalid pointer` crash may occur during PHP shutdown (after compilation succeeds). The crash is in `zend_interned_strings_deactivate` → `zend_hash_destroy(CG(interned_strings))`.

**Root cause**: Swoole's MINIT calls `zend_string_init(..., persistent=1)` + `zend_new_interned_string()` for ~70 known strings (see `ext-src/php_swoole_cxx.cc:21-28` in swoole source). Combined with curl's persistent string allocations, the ZendMM heap layout shifts such that a minor partition overflow in OpKit's persist phase corrupts the `CG(interned_strings)` hash table's internal data.

**Workaround**: phpc compilation does not need swoole or curl. Use one of:

```bash
# Option 1: -n (cleanest — no php.ini, only opkit)
/usr/local/php82/bin/php -n -d zend_extension=opkit bin/phpc -f -s src/ -o dist/

# Option 2: Separate ini without swoole/curl
grep -v '^extension=curl\|^extension=swoole' /usr/local/php82/etc/php.ini > /usr/local/php82/etc/phpc.ini
/usr/local/php82/bin/php -c /usr/local/php82/etc/phpc.ini bin/phpc -f -s src/ -o dist/
```

### PHP 8.2 NULL Opcode Handler (debug builds only)

PHP 8.2 debug builds may trigger `ZEND_ASSERT(zv != NULL)` in `zend_serialize_opcode_handler` when compiling interface-heavy files. Release builds are unaffected. This is a PHP 8.2 compiler issue where `ZEND_COMPILE_DELAYED_BINDING` leaves opcode handlers unset on some interface abstract method opcodes.

### PHP 8.2 Uninitialized op_array Fields

PHP 8.2's `init_op_array` does not initialize all tail fields of `zend_op_array` (notably `cache_size`, `num_dynamic_func_defs`, `attributes`, `static_variables`). The ZendMM-reused memory may contain garbage values. OpKit's persist code guards against these with sanity checks (`> 0 && < 1MB`, `!= (void*)-1`).

### PHP 8.2 Large File Compilation (production builds)

Large classes with 20+ methods on PHP 8.2 release builds may cause `zend_mm_heap corrupted` due to uninitialized struct tail fields overflowing partition calculations. This is a PHP 8.2 compiler defect. **Workaround**: compile with PHP 8.5 (recommended), or reduce class size. Simple files (fewer methods, no deep inheritance) compile correctly on all PHP versions. The debug build (php-src/php-8.2.30) has additional assertions that catch the corruption earlier.

### PHP 8.2 Load Crash (cross-process)

Loading `.phpc` files compiled on a different PHP process may segfault on PHP 8.2 release builds if the file contains typed parameters/returns whose type names were PHP-interned but not properly serialized. The `SERIALIZE_STR` macro now handles `ZSTR_IS_INTERNED` strings via `zend_file_cache_serialize_interned`. Same-process compilation+loading works correctly across all versions.

## OpKit vs OPcache — Architecture Differences

OpKit is heavily based on OPcache's `zend_file_cache.c` + `zend_persist.c`. Key intentional divergences:

| Area | OPcache | OpKit | Reason |
|------|---------|-------|--------|
| **serialize_type** | `SERIALIZE_STR(type_name)` | Also checks `ZSTR_IS_INTERNED` → uses `zend_file_cache_serialize_interned` | PHP-interned strings outside persist block give negative offsets |
| **store_string release** | `zend_string_release_ex(str, 0)` | `zend_string_release_ex(str, GC_FLAGS & IS_STR_PERSISTENT)` | Correctly handles persistent strings from extensions (swoole) |
| **zend_set_str_gc_flags** | `GC_SET_REFCOUNT(str, 2)` | `GC_SET_REFCOUNT(str, 0)` | OpKit manually manages xlat lifecycle |
| **hash_persist** | Immutable array uses `zend_shared_memdup` (no efree) | Always efrees old_data (guarded by null check) | OpKit doesn't use shared memory; always safe to free |
| **op_array doc_comment** | Guarded by `save_comments` directive | Always persisted | phpc always wants comments preserved for stubs |
| **serialize opcode handler** | No null guard | `if (handler && handler != -1)` on serialize side only | PHP 8.2 DELAYED_BINDING may leave NULL handlers |

Missing features (low priority, no runtime impact for current use cases):

| Missing | Impact |
|---------|--------|
| Shared op_array dedup in serialize/unserialize | Larger .phpc files for inherited methods |
| Compaction in `zend_file_cache_serialize_hash` | Slightly larger hashtable serialization |
| `save_comments` directive support | Comments always persisted (desired for phpc) |
