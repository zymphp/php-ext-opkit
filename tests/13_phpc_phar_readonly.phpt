--TEST--
OpKit: phpc tool phar.readonly check
--SKIPIF--
<?php if (!extension_loaded('opkit')) die('skip opkit extension not loaded'); ?>
<?php if (!extension_loaded('phar')) die('skip phar extension not loaded'); ?>
--FILE--
<?php
$phpc = __DIR__ . '/../bin/phpc';
$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$output_dir = __DIR__ . '/test_readonly_out';
if (!is_dir($output_dir)) mkdir($output_dir);

echo "Testing with phar.readonly=1:\n";
// 强制开启 phar.readonly
$cmd = "$php $args -d phar.readonly=1 $phpc -o $output_dir -p test.phar 2>&1";
system($cmd);

echo "\nTesting with phar.readonly=0:\n";
// 关闭 phar.readonly
$cmd = "$php $args -d phar.readonly=0 $phpc -o $output_dir -p test.phar 2>&1";
system($cmd);

// 清理
@unlink($output_dir . '/test.phar');
@rmdir($output_dir);
?>
--EXPECTF--
Testing with phar.readonly=1:
[ERROR] phar.readonly is On, modification or creation of Phar files is prohibited.
Please set phar.readonly=Off in php.ini, or add command line argument -d phar.readonly=0
Example: php -d phar.readonly=0 phpc -s src -o output -p app.phar

Testing with phar.readonly=0:
Packaging directory '%s' to 'test.phar'...
[SUCCESS] Phar file created: %s
--------------------------------------------------
%a
--------------------------------------------------
