--TEST--
OpKit: opkit_compile_file without base_path (backwards compat, flat output)
--EXTENSIONS--
opkit
--FILE--
<?php
$base_dir = __DIR__ . "/test_32_no_basepath_src";
$output_dir = __DIR__ . "/test_32_no_basepath_out";
@mkdir($base_dir . "/deep/nested", 0777, true);
@mkdir($output_dir, 0777, true);

file_put_contents($base_dir . "/deep/nested/DeepClass.php", <<<'PHP'
<?php
namespace Deep\Nested;
class DeepClass {
    public static function run(): string {
        return "deep nested ok";
    }
}
PHP
);

file_put_contents($base_dir . "/app.php", <<<'PHP'
<?php
function main(): int {
    echo \Deep\Nested\DeepClass::run(), "\n";
    return 0;
}
PHP
);

// Compile WITHOUT explicit base_path (2 args) — output should be flat
$r1 = opkit_compile_file($output_dir, $base_dir . "/deep/nested/DeepClass.php");
$r2 = opkit_compile_file($output_dir, $base_dir . "/app.php");

var_dump($r1, $r2);

// Verify output is flat (no directory structure preserved)
var_dump(file_exists($output_dir . "/DeepClass.phpc"));
var_dump(file_exists($output_dir . "/app.phpc"));

// Load and execute
opkit_load($output_dir . "/DeepClass.phpc");
opkit_load($output_dir . "/app.phpc");
opkit_boot();

// Cleanup
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
bool(true)
bool(true)
bool(true)
bool(true)
deep nested ok
--CLEAN--
<?php
function rmrf($dir) {
    if (!is_dir($dir)) return;
    $files = array_diff(scandir($dir), array('.', '..'));
    foreach ($files as $file) {
        (is_dir("$dir/$file")) ? rmrf("$dir/$file") : unlink("$dir/$file");
    }
    return rmdir($dir);
}
@rmrf(__DIR__ . "/test_32_no_basepath_src");
@rmrf(__DIR__ . "/test_32_no_basepath_out");
?>
