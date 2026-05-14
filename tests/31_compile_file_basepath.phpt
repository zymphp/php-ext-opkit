--TEST--
OpKit: opkit_compile_file with explicit base_path preserves directory structure
--EXTENSIONS--
opkit
--FILE--
<?php
$base_dir = __DIR__ . "/test_31_basepath_src";
$output_dir = __DIR__ . "/test_31_basepath_out";
@mkdir($base_dir . "/sub/dir", 0777, true);
@mkdir($output_dir, 0777, true);

file_put_contents($base_dir . "/sub/dir/Library.php", <<<'PHP'
<?php
namespace App\Sub\Dir;
class Library {
    public static function greet(): string {
        return "Hello from nested directory!";
    }
}
PHP
);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main(): int {
    echo \App\Sub\Dir\Library::greet(), "\n";
    return 0;
}
PHP
);

// Compile with explicit base_path — output should mirror source tree
$r1 = opkit_compile_file($output_dir, $base_dir . "/sub/dir/Library.php", $base_dir);
$r2 = opkit_compile_file($output_dir, $base_dir . "/main.php", $base_dir);

var_dump($r1, $r2);

// Verify output directory structure
var_dump(file_exists($output_dir . "/sub/dir/Library.phpc"));
var_dump(file_exists($output_dir . "/main.phpc"));

// Load and execute
opkit_load($output_dir . "/sub/dir/Library.phpc");
opkit_load($output_dir . "/main.phpc");
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
Hello from nested directory!
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
@rmrf(__DIR__ . "/test_31_basepath_src");
@rmrf(__DIR__ . "/test_31_basepath_out");
?>
