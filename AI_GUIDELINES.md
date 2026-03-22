# OpKit AI Agent Development Guidelines

OpKit is a PHP extension designed to provide **offline Opcode persistence** and **pre-compilation** capabilities, heavily inspired by Zend OPcache.

---

## 1. Project Overview
- **Core Functionality**: Compiles PHP scripts into binary `.phpc` files (Opcodes + Symbols) for rapid loading without re-parsing.
- **Key Files**:
    - `src/opkit_module.c`: Extension entry, lifecycle (MINIT, MSHUTDOWN, RINIT, RSHUTDOWN).
    - `src/opkit_compile.c`: Compilation engine, logic for saving/loading scripts.
    - `src/opkit_zend_persist.c`: Memory calculation and data copying to persistent storage.
    - `src/opkit.stub.php`: PHP API definitions (processed by `build/gen_stub.php` to generate `src/opkit_arginfo.h`).
    - `bin/phpc`: The primary CLI tool for project-wide compilation and packaging.

---

## 2. Environment & Build Commands
- **Configure Environment**:
    ```bash
    ./buildconf -f
    ./configure --enable-debug
    ```
- **Build Extension**:
    ```bash
    make -j8 build-modules
    ```
- **Build CLI (PHP)**:
    ```bash
    make -j8 cli
    ```
- **Run Tests**:
    ```bash
    make test TESTS=ext/opkit
    ```

---

## 3. Deployment & Runtime
- **Loading Mode**: **Must** be loaded as a `Zend Extension`.
- **Incompatibility**: OpKit is **strictly incompatible** with `Zend OPcache`. Both should never be enabled simultaneously.
- **INI Configuration (Recommended for Dev)**:
    ```ini
    zend_extension=modules/opkit.so
    phar.readonly=Off
    ```

---

## 4. Coding Standards & Conventions
- **API Changes**:
    - Always modify `src/opkit.stub.php` first.
    - Run `make` to regenerate `src/opkit_arginfo.h` using `build/gen_stub.php` (from PHP source).
- **Memory Management**:
    - Persistent memory is allocated via `ZCG(mem)` (shadow partition).
    - Use logic-partitioning macros for memory calculation: `ADD_SIZE_MD`, `ADD_SIZE_CD`, `ADD_SIZE_DT`, `ADD_SIZE_MS`.
- **Versioning**: Follow `0.0.1-dev` (Current).
- **Phar Support**:
    - OpKit automatically fixes relative paths within Phar archives during `opkit_boot`.
    - `phpc` handles Phar compression and signing.

---

## 5. Development Pitfalls (Common Traps)
- **Symbol Registration**: When registering classes/functions in `opkit_boot`, existing symbols are replaced.
- **Double-Free Risks**: During `RSHUTDOWN`, ensure that the `opkit_script_node` and associated memory are cleaned up exactly once.
- **Constants**: Dynamic `define()` constants are handled by executing the `main_op_array` of the loaded script inside `opkit_boot`.
- **Static Analysis**:
    - `phpc --stubs <dir>` generates PHP stubs from binary `.phpc` files for IDE support.
    - `stubs/opkit.php` provides IDE definitions for the extension's core C functions, exposed via Composer `autoload.files`.

---

## 6. Testing Requirements
- All new features **must** include a corresponding `.phpt` file in `ext/opkit/tests/`.
- Verify both **CLI** and **Phar** execution modes.
- Use `phpc analyze <dir>` to check for symbol conflicts in large projects.

---

## 7. Contacts
- **Author**: Eno-CN <Eno_CN@qq.com>
- **Assistant**: AI Agent (Junie)

*Stay consistent with PHP Internal standards and always verify builds with `--enable-debug`.*
