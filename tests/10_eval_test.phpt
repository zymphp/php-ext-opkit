--TEST--
OpKit: Compilation and execution with eval()
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . "/10_eval_test_src.php";
$output = __DIR__ . "/10_eval_test_src.phpc";

$code = <<<'PHP'
<?php
function test_eval() {
    $val = "hello eval";
    // 测试简单的变量访问
    eval('echo $val . "\n";');
    // 测试在 eval 中定义函数 (带上重复定义检查)
    eval('if (!function_exists("eval_func")) { function eval_func() { echo "func in eval\n"; } }');
    if (function_exists('eval_func')) {
        eval_func();
    } else {
        echo "eval_func NOT found\n";
    }
}

function main() {
    test_eval();
    // 测试在 eval 中执行顶层逻辑
    eval('echo "main eval\n";');

    // 测试在 eval 中定义类
    eval('if (!class_exists("EvalClass")) { class EvalClass { public function hello() { echo "class in eval\n"; } } }');
    if (class_exists('EvalClass')) {
        $obj = new EvalClass();
        $obj->hello();
    } else {
        echo "EvalClass NOT found\n";
    }

    // 测试在 eval 中调用已持久化的函数
    eval('test_eval();');

    return 0;
}
PHP;

file_put_contents($src, $code);

if (opkit_compile_file(__DIR__, $src)) {
    if (opkit_load($output)) {
        opkit_boot();
    }
}

@unlink($src);
@unlink($output);
?>
--EXPECT--
hello eval
func in eval
main eval
class in eval
hello eval
func in eval
