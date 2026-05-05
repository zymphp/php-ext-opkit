--TEST--
OpKit: opkit_is_loaded should detect whether .phpc file is loaded
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . '/23_is_loaded_src.php';
$output = __DIR__ . '/23_is_loaded_src.phpc';

$code = <<<'PHP'
<?php
function hello23() {
    return "hello from 23";
}
PHP;

file_put_contents($src, $code);

// Before loading: should be false
echo "Before load: ";
var_dump(opkit_is_loaded($output));

// Also test with .php extension
echo "Before load (.php): ";
var_dump(opkit_is_loaded($src));

// Compile and load
opkit_compile_file(__DIR__, $src);
$loaded = opkit_load($output);
echo "Load result: ";
var_dump($loaded);

// After loading: should be true
echo "After load (.phpc): ";
var_dump(opkit_is_loaded($output));

echo "After load (.php): ";
var_dump(opkit_is_loaded($src));

// Non-existent file
echo "Non-existent: ";
var_dump(opkit_is_loaded(__DIR__ . '/nonexistent.phpc'));

// File that exists on disk but was never loaded
file_put_contents(__DIR__ . '/23_unloaded.php', '<?php');
opkit_compile_file(__DIR__, __DIR__ . '/23_unloaded.php');
echo "Unloaded file: ";
var_dump(opkit_is_loaded(__DIR__ . '/23_unloaded.phpc'));

// Cleanup
@unlink($src);
@unlink($output);
@unlink(__DIR__ . '/23_unloaded.php');
@unlink(__DIR__ . '/23_unloaded.phpc');
@unlink(__DIR__ . '/entry.php');
?>
--EXPECT--
Before load: bool(false)
Before load (.php): bool(false)
Load result: bool(true)
After load (.phpc): bool(true)
After load (.php): bool(true)
Non-existent: bool(false)
Unloaded file: bool(false)
