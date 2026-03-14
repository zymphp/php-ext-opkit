--TEST--
OpKit: Basic compilation and execution
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . "/01_basic_src.php";
$output = __DIR__ . "/01_basic_src.phpc";

$code = <<<'PHP'
<?php
function main() {
    echo "Hello from OpKit!\n";
    $i = 0;
    while((++$i) <= 5)
    {
        echo 'cycle ', $i, PHP_EOL;
    }
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
Hello from OpKit!
cycle 1
cycle 2
cycle 3
cycle 4
cycle 5
