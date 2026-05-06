--TEST--
OpKit: PHP 8.5 first-class callable in constant support
--EXTENSIONS--
opkit
--SKIPIF--
<?php
if (PHP_VERSION_ID < 80500) {
    die("skip requires PHP 8.5+");
}
?>
--FILE--
<?php

$test_script = <<<'PHP'
<?php

// First-class callable in constant expressions (PHP 8.5 feature)
const FCC_CONST = strtoupper(...);

function main(): int {
    printf("FCC_CONST = %s\n", (FCC_CONST)('world'));
    echo "PHP 8.5 FCC test OK\n";
    return 0;
}
PHP;

$path = __DIR__ . '/tmp_test85.php';
$output = $path . 'c';
file_put_contents($path, $test_script);

$compiled = opkit_compile_file(__DIR__, $path);
var_dump($compiled);

$loaded = opkit_load($output);
var_dump($loaded);

$booted = opkit_boot();
var_dump($booted);

@unlink($path);
@unlink($output);
?>
--EXPECTF--
bool(true)
bool(true)
FCC_CONST = WORLD
PHP 8.5 FCC test OK
int(0)%A
