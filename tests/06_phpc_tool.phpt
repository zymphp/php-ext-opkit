--TEST--
OpKit: phpc tool functionality
--EXTENSIONS--
opkit
phar
--INI--
phar.readonly=0
--FILE--
<?php
$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
// Ensure opkit is loaded in sub-processes
if (!strpos($args, 'extension=opkit')) {
    $ext_so = dirname(__DIR__) . "/modules/opkit.so";
    if (file_exists($ext_so)) {
        $args .= " -d extension=" . escapeshellarg($ext_so);
    }
}
$phpc = dirname(__DIR__) . "/bin/phpc";

// 准备源文件
$base_dir = __DIR__ . "/test_06_src";
if (!is_dir($base_dir)) mkdir($base_dir, 0777, true);
file_put_contents($base_dir . "/hello.php", <<<'PHP'
<?php
function sayHello() {
    echo "Hello from OpKit!\n";
}
class ToolTester {
    public function test() {
        echo "ToolTester is working\n";
    }
}
PHP
);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main() {
    sayHello();
    $t = new ToolTester();
    $t->test();
    return 0;
}
PHP
);

$output_dir = __DIR__ . "/test_06_target";
$phar_file = __DIR__ . "/test_tool.phar";

// 1. 编译并打包
// 注意：在测试中直接引用模块路径可能不太可靠，
// 但在 php-src 的测试框架中，通常我们可以通过环境变量或者当前路径获取。
// 这里我们尝试直接执行，假设 sapi/cli/php 已经在环境中或 PHP_BINARY 可用
$cmd = "$php $args -d phar.readonly=0 $phpc -s $base_dir -o $output_dir -p $phar_file 2>&1";
exec($cmd, $output, $return_var);

echo "Build return code: $return_var\n";
if ($return_var !== 0) {
    echo implode("\n", $output) . "\n";
}

// 2. 运行 Phar
if (file_exists($phar_file)) {
    // 使用子进程运行 Phar，因为 Phar 的入口脚本包含 exit()，在当前进程 include 会导致测试提前结束。
    $cmd_phar = "$php $args $phar_file 2>&1";
    passthru($cmd_phar);
} else {
    echo "Error: Phar file not created!\n";
}

// 3. 测试 phpc -i
$phpc_file = $output_dir . "/hello.phpc";
echo "Checking for phpc file: $phpc_file\n";
if (file_exists($phpc_file)) {
    $cmd_info = "$php $args $phpc -i $phpc_file 2>&1";
    $output_info = [];
    exec($cmd_info, $output_info);

    $found_info = false;
    $found_func = false;
    $found_class = false;
    foreach ($output_info as $line) {
        if (strpos($line, "OpKit .phpc file information") !== false) $found_info = true;
        if (strpos($line, "sayHello") !== false) $found_func = true;
        if (strpos($line, "ToolTester") !== false) $found_class = true;
    }

    echo "phpc -i success: " . ($found_info ? "YES" : "NO") . "\n";
    echo "Found function sayHello: " . ($found_func ? "YES" : "NO") . "\n";
    echo "Found class ToolTester: " . ($found_class ? "YES" : "NO") . "\n";
} else {
    echo "Error: hello.phpc not found!\n";
}

// 4. 测试 phpc -e
$entry_file = __DIR__ . "/test_06_entry.php";
$cmd_entry = "$php $args $phpc -e $entry_file 2>&1";
exec($cmd_entry, $output_entry, $return_var_entry);
echo "phpc -e return code: $return_var_entry\n";
if (file_exists($entry_file)) {
    $content = file_get_contents($entry_file);
    echo "Entry file contains opkit_boot: " . (strpos($content, "opkit_boot()") !== false ? "YES" : "NO") . "\n";
    unlink($entry_file);
}

// 清理
function rmrf($dir) {
    if (!is_dir($dir)) return;
    $files = array_diff(scandir($dir), array('.', '..'));
    foreach ($files as $file) {
        (is_dir("$dir/$file")) ? rmrf("$dir/$file") : unlink("$dir/$file");
    }
    return rmdir($dir);
}
rmrf($base_dir);
rmrf($output_dir);
if (file_exists($phar_file)) unlink($phar_file);
?>
--EXPECTF--
Build return code: 0
%A
Hello from OpKit!
ToolTester is working
Checking for phpc file: %s/test_06_target/hello.phpc
phpc -i success: YES
Found function sayHello: YES
Found class ToolTester: YES
phpc -e return code: 0
Entry file contains opkit_boot: YES
