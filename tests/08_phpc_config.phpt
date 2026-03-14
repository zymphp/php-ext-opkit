--TEST--
OpKit: phpc tool with config file
--SKIPIF--
<?php
if (!extension_loaded('opkit')) die('skip opkit extension not loaded');
?>
--FILE--
<?php
$workDir = __DIR__ . DIRECTORY_SEPARATOR . 'test_08_work';
if (!is_dir($workDir)) mkdir($workDir);
$srcDir = $workDir . DIRECTORY_SEPARATOR . 'src';
$outDir = $workDir . DIRECTORY_SEPARATOR . 'out';
if (!is_dir($srcDir)) mkdir($srcDir);
if (!is_dir($outDir)) mkdir($outDir);

file_put_contents($srcDir . DIRECTORY_SEPARATOR . 'test.php', '<?php echo "Hello Config\n";');

// 创建配置文件
$config = [
    'src' => 'src',
    'output' => 'out',
    'phar' => 'test_config.phar'
];
file_put_contents($workDir . DIRECTORY_SEPARATOR . 'opkit.json', json_encode($config));

$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$phpc = __DIR__ . '/../bin/phpc';
$phar_off = "-d phar.readonly=Off";

// 在工作目录下运行 phpc，不带参数（应该自动读 opkit.json）
echo "Running phpc with config...\n";
chdir($workDir);
passthru("$php $args -d phar.readonly=Off $phpc 2>&1", $return_var);

if (file_exists($outDir . DIRECTORY_SEPARATOR . 'test.phpc')) {
    echo "test.phpc exists\n";
}
if (file_exists($workDir . DIRECTORY_SEPARATOR . 'test_config.phar')) {
    echo "test_config.phar exists\n";
}

// 清理
unlink($srcDir . DIRECTORY_SEPARATOR . 'test.php');
unlink($outDir . DIRECTORY_SEPARATOR . 'test.phpc');
unlink($outDir . DIRECTORY_SEPARATOR . 'entry.php');
unlink($workDir . DIRECTORY_SEPARATOR . 'test_config.phar');
unlink($workDir . DIRECTORY_SEPARATOR . 'opkit.json');
rmdir($srcDir);
rmdir($outDir);
rmdir($workDir);
?>
--EXPECTF--
Running phpc with config...
--------------------------------------------------
OpKit is compiling (incremental mode)...
Source: %s/src
Output: %s/out
--------------------------------------------------
[COMPILE] test.php (%fs)

Compilation complete: Success 1, Skipped 0, Failed 0 (Total time: %fs)
[SUCCESS] Bootstrap entry file generated: %s/out/entry.php
Packaging directory '%s/out' to 'test_config.phar'...
[SUCCESS] Phar file created: %s/test_config.phar
--------------------------------------------------
You can run the Phar file directly to start the application:
php -d zend_extension=opkit.so test_config.phar
--------------------------------------------------
test.phpc exists
test_config.phar exists
