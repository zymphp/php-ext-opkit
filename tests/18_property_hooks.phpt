--TEST--
OpKit: PHP 8.4 Property Hooks support
--SKIPIF--
<?php
if (!extension_loaded('opkit')) die('skip opkit extension not loaded');
if (PHP_VERSION_ID < 80400) die('skip requires PHP 8.4+');
?>
--FILE--
<?php
// Test property hooks compilation (PHP 8.4+)
$testDir = __DIR__ . '/phpc_test_property_hooks_' . getmypid();
@mkdir($testDir);

$code = <<<'PHPCODE'
<?php
class User {
    public string $name {
        get => $this->name;
        set => $this->name = trim($value);
    }

    public int $age {
        get {
            return $this->age;
        }
        set {
            if ($value < 0) {
                throw new ValueError("Age cannot be negative");
            }
            $this->age = $value;
        }
    }

    public function __construct(string $name, int $age) {
        $this->name = $name;
        $this->age = $age;
    }
}

function main() {
    $user = new User("  John Doe  ", 25);
    echo "Name: " . $user->name . "\n";
    echo "Age: " . $user->age . "\n";
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

// Test execution with property hooks
if ($result && file_exists($phpcFile)) {
    echo "Loading...\n";
    opkit_load($phpcFile);
    echo "Booting...\n";
    $exitCode = opkit_boot();
    echo "Exit code: " . ($exitCode ?? 0) . "\n";
}

// Clean up
@unlink($srcFile);
@unlink($phpcFile);
@rmdir($testDir);

echo "Property hooks test completed\n";
?>
--EXPECT--
Compilation: OK
Phpc file exists: YES
Loading...
Booting...
Name: John Doe
Age: 25
Exit code: 0
Property hooks test completed
