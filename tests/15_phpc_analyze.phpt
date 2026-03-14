--TEST--
OpKit: phpc analyze command and symbol conflicts
--SKIPIF--
<?php if (!extension_loaded("opkit")) print "skip"; ?>
--FILE--
<?php
$src = __DIR__ . "/test_15_src";
$target = __DIR__ . "/test_15_target";
@mkdir($src);
@mkdir($target);

file_put_contents($src . "/a.php", "<?php function duplicate_func() {} class DuplicateClass {}");
file_put_contents($src . "/b.php", "<?php function duplicate_func() {} class DuplicateClass {}");
file_put_contents($src . "/unique.php", "<?php function unique_func() {}");

// Use phpc to compile
$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$phpc = __DIR__ . "/../bin/phpc";

$cmd = "$php $args $phpc -s " . escapeshellarg($src) . " -o " . escapeshellarg($target) . " -f";
passthru($cmd);

echo "--- Testing -a flag ---\n";
passthru("$php $args $phpc -a " . escapeshellarg($target));

echo "\n--- Testing analyze subcommand ---\n";
passthru("$php $args $phpc analyze " . escapeshellarg($target));

// Cleanup
@unlink($src . "/a.php");
@unlink($src . "/b.php");
@unlink($src . "/unique.php");
@rmdir($src);
foreach (glob("$target/*") as $f) @unlink($f);
@rmdir($target);
?>
--EXPECTF--
--------------------------------------------------
OpKit is compiling (incremental mode) [force]...
Source: %s
Output: %s
--------------------------------------------------
[COMPILE] %s (%fs)
[COMPILE] %s (%fs)
[COMPILE] %s (%fs)

Compilation complete: Success 3, Skipped 0, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s
--- Testing -a flag ---
--------------------------------------------------
OpKit Static Analysis Report
Target: %s
--------------------------------------------------
Total Files     : 3
Total Memory    : %s bytes
Total Symbols   : 2 functions, 1 classes, 0 constants
--------------------------------------------------
Logical Subdivision Summary:
  Metadata Area : %s
  Code Area     : %s
  Data Area     : %s
  Misc Area     : %s
--------------------------------------------------
[WARNING] Symbol Conflicts Found:
The following symbols are defined in multiple files, which will cause fatal errors during opkit_boot().
  - Functions: duplicate_func (defined in: a.phpc, b.phpc)
  - Classes: DuplicateClass (defined in: a.phpc, b.phpc)
--------------------------------------------------

--- Testing analyze subcommand ---
--------------------------------------------------
OpKit Static Analysis Report
Target: %s
--------------------------------------------------
Total Files     : 3
Total Memory    : %s bytes
Total Symbols   : 2 functions, 1 classes, 0 constants
--------------------------------------------------
Logical Subdivision Summary:
  Metadata Area : %s
  Code Area     : %s
  Data Area     : %s
  Misc Area     : %s
--------------------------------------------------
[WARNING] Symbol Conflicts Found:
The following symbols are defined in multiple files, which will cause fatal errors during opkit_boot().
  - Functions: duplicate_func (defined in: a.phpc, b.phpc)
  - Classes: DuplicateClass (defined in: a.phpc, b.phpc)
--------------------------------------------------
