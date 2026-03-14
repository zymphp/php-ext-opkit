--TEST--
OpKit: Constants support
--SKIPIF--
<?php
if (!extension_loaded('OPkit')) die('skip OPkit extension not loaded');
if (!file_exists(__DIR__ . '/../bin/phpc')) die('skip phpc tool not found');
?>
--FILE--
<?php
$phpc = __DIR__ . '/../bin/phpc';
$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$tmp_ini = __DIR__ . '/../../tmp-dev.ini';
$src_dir = __DIR__ . '/test_07_src';
$target_dir = __DIR__ . '/test_07_target';

@mkdir($src_dir);
@mkdir($target_dir);

file_put_contents($src_dir . '/constants.php', <<<'PHP'
<?php
define('OPKIT_TEST_CONST_STRING', 'hello opkit');
define('OPKIT_TEST_CONST_INT', 12345);
define('OPKIT_TEST_CONST_ARRAY', ['a', 'b', 'c']);

class TestConst {
    const CLASS_CONST = "class constant value";
}

function main() {
    echo "String const: " . OPKIT_TEST_CONST_STRING . "\n";
    echo "Int const: " . OPKIT_TEST_CONST_INT . "\n";
    echo "Array const: " . implode(',', OPKIT_TEST_CONST_ARRAY) . "\n";
    echo "Class const: " . TestConst::CLASS_CONST . "\n";
    return 0;
}
PHP
);

$cmd = "$php $args $phpc -s $src_dir -o $target_dir 2>&1";
exec($cmd, $output, $return_var);
if ($return_var !== 0) {
    echo "Compilation failed\n";
    echo implode("\n", $output) . "\n";
    exit(1);
}

// 运行生成的入口文件
$entry_php = $target_dir . '/entry.php';
$cmd = "$php $args $entry_php 2>&1";
passthru($cmd);

// 清理
function rm_dir($dir) {
    if (!is_dir($dir)) return;
    $files = array_diff(scandir($dir), array('.', '..'));
    foreach ($files as $file) {
        $path = "$dir/$file";
        is_dir($path) ? rm_dir($path) : unlink($path);
    }
    rmdir($dir);
}
rm_dir($src_dir);
rm_dir($target_dir);
?>
--EXPECT--
String const: hello opkit
Int const: 12345
Array const: a,b,c
Class const: class constant value
