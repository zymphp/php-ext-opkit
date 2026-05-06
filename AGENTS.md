# AGENTS.md — OpKit

OpKit is a PHP Zend Extension for offline Opcode pre-compilation (`.phpc` binary files).

## Build

```bash
# Use the matching phpize/php-config from php-src/. Example for PHP 8.5:
php-src/php-8.5.4/scripts/phpize && \
./configure --with-php-config=php-src/php-8.5.4/scripts/php-config && \
make
```

`config.m4` requires PHP 8.2–8.5. PHP 8.6+ is not yet supported.
The extension is always shared; `--enable-debug` is supported for ZEND_DEBUG builds.

## Test

```bash
# Run directly with the PHP test runner:
php-src/php-8.5.4/sapi/cli/php -d zend_extension=$(pwd)/modules/opkit.so \
  php-src/php-8.5.4/run-tests.php -d zend_extension=$(pwd)/modules/opkit.so tests/
```

Tests use `.phpt` format. Check failures in `tests/*.diff` and `tests/*.out`.
`ext/opkit/tests/` is empty; real tests are in root `tests/`.

Test results (2026-05-06): **0 failing across PHP 8.2/8.3/8.4/8.5** (100% of non-skipped).

| PHP | Pass | Skip | Fail | Rate |
|-----|------|------|------|------|
| 8.2 | 22 | 3 | 0 | 100% |
| 8.3 | 22 | 3 | 0 | 100% |
| 8.4 | 23 | 2 | 0 | 100% |
| 8.5 | 24 | 1 | 0 | 100% |

Skipped: `05_triple_des.phpt` (requires openssl), `18_property_hooks.phpt` (PHP 8.4+), `24_php85_fcc_const.phpt` (PHP 8.5+).

## Architecture Limitation

PHP's compiler arena (`ast_arena`) is destroyed by `zend_compile()` before OpKit's persistence runs. To prevent dangling arena pointers, `opkit_compile_file()` now pre-resolves all `IS_CONSTANT_AST` values in class properties, class constants, and op_array literals by calling `zval_update_constant_ex()` after registering file-level constants in `EG(zend_constants)`. This converts constant references to their resolved values before the persist phase, eliminating both stale AST pointer access and memory leaks.

Remaining edge cases:
- Class constant array keys using `self::CONST` where the class isn't fully linked at resolve time
- Constants referencing other constants from a different file (not yet loaded)

OpKit previously used `ZEND_COMPILE_NO_CONSTANT_SUBSTITUTION` (matching OPcache) but this prevented PHP's `pass_two()` from resolving these ASTs at compile time. Removing this flag allows most constant expressions to resolve correctly, but some edge cases remain.

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
- `doc_comment` was removed from `zend_property_info` in PHP 8.4.
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
