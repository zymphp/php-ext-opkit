--TEST--
OpKit: Stub generation support
--SKIPIF--
<?php if (!extension_loaded('opkit')) die('skip opkit extension not loaded'); ?>
--FILE--
<?php
$src_dir = __DIR__ . '/test_stubs_src';
$out_dir = __DIR__ . '/test_stubs_out';
$stub_dir = __DIR__ . '/test_stubs_gen';

if (!is_dir($src_dir)) mkdir($src_dir);
if (!is_dir($out_dir)) mkdir($out_dir);

$code = <<<'PHP'
<?php
namespace App;
const MY_CONST = 'val';
class MyClass {
    const MY_CONST = 'val';
    public int $myProp = 1;
    public function myMethod(string $arg): bool {
        return true;
    }
}
function my_func(int $x): void {}
PHP;

file_put_contents($src_dir . '/test.php', $code);

$php = getenv('TEST_PHP_EXECUTABLE') ?: PHP_BINARY;
$args = getenv('TEST_PHP_EXTRA_ARGS') ?: '';
$phpc = __DIR__ . '/../bin/phpc';
$cmd = "$php $args $phpc -s $src_dir -o $out_dir --stubs $stub_dir -f 2>&1";
exec($cmd, $output, $return_var);

// If there's an error, output the last few lines to debug
if ($return_var !== 0) {
    echo implode("\n", array_slice($output, -5)) . "\n";
}

echo "Build return code: $return_var\n";

$stub_file = $stub_dir . '/test.php';
if (file_exists($stub_file)) {
    echo "Stub file generated.\n";
    $content = file_get_contents($stub_file);
    // Remove extra whitespaces for stable comparison
    $normalized_content = preg_replace('/\s+/', ' ', $content);
    echo "Content contains namespace App: " . (strpos($normalized_content, 'namespace App {') !== false ? 'YES' : 'NO') . "\n";
    echo "Content contains class MyClass: " . (strpos($normalized_content, 'class MyClass {') !== false ? 'YES' : 'NO') . "\n";
    echo "Content contains myMethod(string \$arg): bool: " . (strpos($normalized_content, 'function myMethod(string $arg): bool') !== false ? 'YES' : 'NO') . "\n";
    echo "Content contains my_func(int \$x): void: " . (strpos($normalized_content, 'function my_func(int $x): void') !== false ? 'YES' : 'NO') . "\n";
    echo "Content contains const MY_CONST: " . (strpos($normalized_content, 'const MY_CONST = null;') !== false ? 'YES' : 'NO') . "\n";
} else {
    echo "Stub file NOT generated.\n";
    echo "Output: " . implode("\n", $output) . "\n";
}

// Cleanup
@unlink($src_dir . '/test.php');
@rmdir($src_dir);
@unlink($out_dir . '/test.phpc');
@unlink($out_dir . '/entry.php');
@rmdir($out_dir);
@unlink($stub_file);
@rmdir($stub_dir);

?>
--EXPECT--
Build return code: 0
Stub file generated.
Content contains namespace App: YES
Content contains class MyClass: YES
Content contains myMethod(string $arg): bool: YES
Content contains my_func(int $x): void: YES
Content contains const MY_CONST: YES
