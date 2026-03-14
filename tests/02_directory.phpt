--TEST--
OpKit: Directory recursive compilation
--EXTENSIONS--
opkit
--FILE--
<?php
$base_dir = __DIR__ . "/test_02_src";
@mkdir($base_dir);
@mkdir($base_dir . "/utils");

file_put_contents($base_dir . "/utils/Math.php", <<<'PHP'
<?php
class Math {
    public static function add($a, $b) {
        return $a + $b;
    }
}
PHP
);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main() {
    $a = 10;
    $b = 20;
    $sum = Math::add($a, $b);
    echo "The sum of $a and $b is: $sum\n";
    return 0;
}
PHP
);

$output_dir = __DIR__ . "/test_02_target";
@mkdir($output_dir);

if (opkit_compile_dir($output_dir, $base_dir)) {
    opkit_load($output_dir . "/utils/Math.phpc");
    opkit_load($output_dir . "/main.phpc");
    opkit_boot();
}

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
?>
--EXPECT--
The sum of 10 and 20 is: 30
