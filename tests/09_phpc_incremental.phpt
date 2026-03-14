--TEST--
OpKit: phpc tool with incremental compilation
--SKIPIF--
<?php
if (!extension_loaded('opkit')) die('skip opkit extension not loaded');
?>
--FILE--
<?php
$workDir = __DIR__ . DIRECTORY_SEPARATOR . 'test_09_work';
if (!is_dir($workDir)) mkdir($workDir);
$srcDir = $workDir . DIRECTORY_SEPARATOR . 'src';
$outDir = $workDir . DIRECTORY_SEPARATOR . 'out';
if (!is_dir($srcDir)) mkdir($srcDir);
if (!is_dir($outDir)) mkdir($outDir);

$srcFile = $srcDir . DIRECTORY_SEPARATOR . 'test.php';
file_put_contents($srcFile, '<?php echo "Hello Incremental\n";');

$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$phpc = __DIR__ . '/../bin/phpc';

// 1. 第一次编译
echo "First compilation:\n";
passthru("$php $args $phpc -s $srcDir -o $outDir 2>&1", $return_var);

// 2. 第二次编译，没有修改源码，应该跳过
echo "\nSecond compilation (no changes):\n";
passthru("$php $args $phpc -s $srcDir -o $outDir 2>&1", $return_var);

// 3. 修改源码并编译，应该重新编译
echo "\nThird compilation (after change):\n";
sleep(1); // 确保 mtime 确实发生变化
file_put_contents($srcFile, '<?php echo "Modified\n";');
passthru("$php $args $phpc -s $srcDir -o $outDir 2>&1", $return_var);

// 4. 强制编译，即使没有修改
echo "\nFourth compilation (force):\n";
passthru("$php $args $phpc -s $srcDir -o $outDir -f 2>&1", $return_var);

// 清理
unlink($srcFile);
unlink($outDir . DIRECTORY_SEPARATOR . 'test.phpc');
unlink($outDir . DIRECTORY_SEPARATOR . 'entry.php');
rmdir($srcDir);
rmdir($outDir);
rmdir($workDir);
?>
--EXPECTF--
First compilation:
--------------------------------------------------
OpKit is compiling (incremental mode)...
Source: %s/src
Output: %s/out
--------------------------------------------------
[COMPILE] test.php (%fs)

Compilation complete: Success 1, Skipped 0, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s/out/entry.php

Second compilation (no changes):
--------------------------------------------------
OpKit is compiling (incremental mode)...
Source: %s/src
Output: %s/out
--------------------------------------------------

Compilation complete: Success 0, Skipped 1, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s/out/entry.php

Third compilation (after change):
--------------------------------------------------
OpKit is compiling (incremental mode)...
Source: %s/src
Output: %s/out
--------------------------------------------------
[COMPILE] test.php (%fs)

Compilation complete: Success 1, Skipped 0, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s/out/entry.php

Fourth compilation (force):
--------------------------------------------------
OpKit is compiling (incremental mode) [force]...
Source: %s/src
Output: %s/out
--------------------------------------------------
[COMPILE] test.php (%fs)

Compilation complete: Success 1, Skipped 0, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s/out/entry.php
