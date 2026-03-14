--TEST--
OpKit: Batch loading with opkit_load_multi and entry.php
--SKIPIF--
<?php if (!extension_loaded("opkit")) print "skip"; ?>
--FILE--
<?php
$src_dir = __DIR__ . "/test_12_src";
$target_dir = __DIR__ . "/test_12_target";

if (!is_dir($src_dir)) mkdir($src_dir);
if (!is_dir($target_dir)) mkdir($target_dir);

file_put_contents($src_dir . "/a.php", "<?php function func_a() { echo 'A'; }");
file_put_contents($src_dir . "/b.php", "<?php function func_b() { echo 'B'; }");
file_put_contents($src_dir . "/main.php", "<?php function main() { func_a(); func_b(); echo ' Done\n'; }");

opkit_compile_dir($target_dir, $src_dir);
opkit_gen_entry_file($target_dir . "/entry.php");

echo "--- Content of entry.php ---\n";
$entry_content = file_get_contents($target_dir . "/entry.php");
if (str_contains($entry_content, 'opkit_load_multi([')) {
    echo "opkit_load_multi found\n";
}
if (str_contains($entry_content, "__DIR__ . '/a.phpc'")) {
    echo "a.phpc found\n";
}
if (str_contains($entry_content, "__DIR__ . '/b.phpc'")) {
    echo "b.phpc found\n";
}

echo "--- Execution ---\n";
include $target_dir . "/entry.php";

?>
--EXPECTF--
--- Content of entry.php ---
opkit_load_multi found
a.phpc found
b.phpc found
--- Execution ---
AB Done
--CLEAN--
<?php
$src_dir = __DIR__ . "/test_12_src";
$target_dir = __DIR__ . "/test_12_target";
array_map('unlink', glob("$src_dir/*.*"));
array_map('unlink', glob("$target_dir/*.*"));
rmdir($src_dir);
rmdir($target_dir);
?>
