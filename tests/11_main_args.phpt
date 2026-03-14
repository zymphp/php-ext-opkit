--TEST--
OpKit: opkit_boot() with dynamic entry point and arguments
--SKIPIF--
<?php if (!extension_loaded("opkit")) print "skip"; ?>
--FILE--
<?php
$src = __DIR__ . "/test_11_src";
$target = __DIR__ . "/test_11_target";
if (!is_dir($src)) mkdir($src);
if (!is_dir($target)) mkdir($target);

file_put_contents($src . "/func.php", '<?php
function my_entry($a, $b) {
    echo "Entry called with: $a, $b\n";
    return $a + $b;
}

function main() {
    echo "Default main called\n";
    return 42;
}
');

// 1. 编译
opkit_compile_dir($target, $src);

// 2. 加载
opkit_load($target . "/func.phpc");

// 3. 测试默认 main()
echo "--- Default main ---\n";
$ret = opkit_boot();
echo "Return: $ret\n";

// 4. 测试指定 entry 不带参数
echo "--- Dynamic entry (no args) ---\n";
try {
    // my_entry 需要两个参数，如果不传会报 ArgumentCountError
    $ret = opkit_boot('my_entry');
} catch (ArgumentCountError $e) {
    echo "Caught: " . $e->getMessage() . "\n";
}

// 5. 测试指定 entry 带参数
echo "--- Dynamic entry (with args) ---\n";
$ret = opkit_boot('my_entry', [10, 20]);
echo "Return: $ret\n";

// 6. 测试闭包
echo "--- Closure ---\n";
$ret = opkit_boot(function($x) {
    echo "Closure called with $x\n";
    return $x * 2;
}, [21]);
echo "Return: $ret\n";

?>
--CLEAN--
<?php
$target = __DIR__ . "/test_11_target";
if (is_dir($target)) {
    array_map('unlink', glob("$target/*.*"));
    rmdir($target);
}
unlink(__DIR__ . "/test_11_src/func.php");
rmdir(__DIR__ . "/test_11_src");
?>
--EXPECTF--
--- Default main ---
Default main called
Return: 42
--- Dynamic entry (no args) ---
Caught: Too few arguments to function my_entry(), 0 passed and exactly 2 expected
--- Dynamic entry (with args) ---
Entry called with: 10, 20
Return: 30
--- Closure ---
Closure called with 21
Return: 42
