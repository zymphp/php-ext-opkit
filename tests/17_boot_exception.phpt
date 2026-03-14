--TEST--
OpKit: opkit_boot() should throw exception when no entry point found
--SKIPIF--
<?php if (!extension_loaded('opkit')) die('skip opkit extension not loaded'); ?>
--FILE--
<?php
// Scenario 1: No script loaded
try {
    opkit_boot();
} catch (Exception $e) {
    echo "Caught: " . $e->getMessage() . "\n";
}

// Scenario 2: Script loaded, but entry point missing
$source = __DIR__ . "/test_no_main.php";
$out_dir = __DIR__ . "/out_test";
@mkdir($out_dir);
file_put_contents($source, '<?php function foo() { echo "foo"; }');

$php_bin = PHP_BINARY;
$ext = __DIR__ . "/../modules/opkit.so";
$phpc = __DIR__ . "/../bin/phpc";

// Compile it
exec("$php_bin -d zend_extension=$ext $phpc -s $source -o $out_dir 2>/dev/null");

opkit_load($out_dir . "/test_no_main.phpc");

try {
    opkit_boot(); // Should fail as no "main" is defined
} catch (Exception $e) {
    echo "Caught: " . $e->getMessage() . "\n";
}

// Clean up
@unlink($source);
@unlink($out_dir . "/test_no_main.phpc");
@rmdir($out_dir);
?>
--EXPECTF--
Caught: No script loaded for opkit_boot
Caught: Entry point "main" not found
