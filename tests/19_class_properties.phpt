--TEST--
OpKit: Class properties support
--SKIPIF--
<?php
if (!extension_loaded('opkit')) die('skip opkit extension not loaded');
?>
--FILE--
<?php
// Test class properties compilation
$testDir = __DIR__ . '/phpc_test_class_props_' . getmypid();
@mkdir($testDir);

$code = <<<'PHPCODE'
<?php
class User {
    public $name;
    public $age;

    public function __construct(string $name, int $age) {
        $this->name = $name;
        $this->age = $age;
    }

    public function getInfo(): string {
        return "Name: {$this->name}, Age: {$this->age}";
    }
}

function main() {
    $user = new User("John Doe", 25);
    echo $user->getInfo() . "\n";
    return 0;
}
PHPCODE;

$srcFile = $testDir . '/test.php';
file_put_contents($srcFile, $code);

// Compile using opkit API
$result = opkit_compile_file($testDir, $srcFile);
echo "Compilation: " . ($result ? "OK" : "FAILED") . "\n";

// Verify .phpc file was created
$phpcFile = $testDir . '/test.phpc';
echo "Phpc file exists: " . (file_exists($phpcFile) ? "YES" : "NO") . "\n";

// Test execution with class properties
if ($result && file_exists($phpcFile)) {
    echo "Loading...\n";
    opkit_load($phpcFile);
    echo "Booting...\n";
    $exitCode = opkit_boot();
    echo "Exit code: " . $exitCode . "\n";
}

// Clean up
@unlink($srcFile);
@unlink($phpcFile);
@rmdir($testDir);

echo "Class properties test completed\n";
?>
--EXPECT--
Compilation: OK
Phpc file exists: YES
Loading...
Booting...
Name: John Doe, Age: 25
Exit code: 0
Class properties test completed
--CLEAN--
<?php
$testDir = __DIR__ . '/phpc_test_class_props_' . getmypid();
@unlink($testDir . '/test.php');
@unlink($testDir . '/test.phpc');
@rmdir($testDir);
?>
