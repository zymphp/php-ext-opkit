<?php

/**
 * OpKit Extension Stub for IDE & Static Analysis
 * @author Eno-CN <Eno_CN@qq.com>
 */

if (!extension_loaded('opkit')) {
    /**
     * Compile a PHP file into an OpKit Opcode file
     * @param string $output_path Output path for the .phpc file
     * @param string $filename Path to the PHP source file
     * @return bool
     */
    function opkit_compile_file(string $output_path, string $filename): bool {}

    /**
     * Compile an entire directory into OpKit Opcode files
     * @param string $output_path Output directory for the .phpc files
     * @param string $dir Path to the source directory
     * @return bool
     */
    function opkit_compile_dir(string $output_path, string $dir): bool {}

    /**
     * Load an OpKit Opcode file
     * @param string $filename Path to the .phpc file
     * @return bool
     */
    function opkit_load(string $filename): bool {}

    /**
     * Load multiple OpKit Opcode files from an array
     * @param string[] $filenames Array of paths to .phpc files
     * @return void
     */
    function opkit_load_multi(array $filenames): void {}

    /**
     * Boot the OpKit and execute the entry point
     * @param callable|string|null $entry Entry point function/method (default: "main")
     * @param array $args Arguments to pass to the entry point
     * @return mixed
     */
    function opkit_boot(callable|string|null $entry = "main", array $args = []): mixed {}

    /**
     * Generate a bootstrap entry Opcode file
     * @param string $output_path Output path for the entry.php file
     * @return bool
     */
    function opkit_gen_entry_file(string $output_path): bool {}

    /**
     * Get information about an OpKit Opcode file
     * @param string $filename Path to the .phpc file
     * @return array|null Metadata about the file or null on failure
     */
    function opkit_get_info(string $filename): ?array {}
}
