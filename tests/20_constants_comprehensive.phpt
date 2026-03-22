--TEST--
OpKit: Comprehensive constants support (define, const, class constants with visibility)
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . '/20_constants_comprehensive_src.php';
$output = __DIR__ . '/20_constants_comprehensive_src.phpc';

$code = <<<'PHP'
<?php
// Namespace-level const declarations
const NAMESPACE_CONST_STRING = 'namespace const value';
const NAMESPACE_CONST_INT = 999;
const NAMESPACE_CONST_FLOAT = 3.14159;
const NAMESPACE_CONST_BOOL = true;
const NAMESPACE_CONST_NULL = null;
const NAMESPACE_CONST_ARRAY = ['key1' => 'value1', 'key2' => [1, 2, 3]];

// define() constants with various types
define('DEFINE_STRING', 'defined string');
define('DEFINE_INT', 42);
define('DEFINE_FLOAT', 2.71828);
define('DEFINE_BOOL', false);
define('DEFINE_NULL', null);
define('DEFINE_ARRAY', ['a', 'b', 'c', 'nested' => ['x', 'y']]);

// Class constants with various visibility modifiers (PHP 7.1+)
class ConstantsDemo {
    public const PUBLIC_CONST = 'public constant';
    protected const PROTECTED_CONST = 'protected constant';
    private const PRIVATE_CONST = 'private constant';

    // Untyped class constants (default public)
    const DEFAULT_CONST = 'default visibility constant';

    // Integer constants
    public const PUBLIC_INT = 100;
    protected const PROTECTED_INT = 200;
    private const PRIVATE_INT = 300;

    // Array constants
    public const PUBLIC_ARRAY = [1, 2, 3];

    public static function getConstants(): array {
        return [
            'public' => self::PUBLIC_CONST,
            'protected' => self::PROTECTED_CONST,
            'private' => self::PRIVATE_CONST,
            'default' => self::DEFAULT_CONST,
            'public_int' => self::PUBLIC_INT,
            'protected_int' => self::PROTECTED_INT,
            'private_int' => self::PRIVATE_INT,
            'public_array' => self::PUBLIC_ARRAY,
        ];
    }
}

// Interface constants
interface IConstants {
    public const INTERFACE_CONST = 'interface constant';
}

// Class implementing interface
class ClassWithInterface implements IConstants {
}

// Trait with constants (PHP 8.2+)
trait TraitConstants {
    public const TRAIT_CONST = 'trait constant';
}

class ClassWithTrait {
    use TraitConstants;
}

function main() {
    // Test namespace constants
    echo "=== Namespace Constants ===\n";
    echo "String: " . NAMESPACE_CONST_STRING . "\n";
    echo "Int: " . NAMESPACE_CONST_INT . "\n";
    echo "Float: " . NAMESPACE_CONST_FLOAT . "\n";
    echo "Bool: " . (NAMESPACE_CONST_BOOL ? 'true' : 'false') . "\n";
    echo "Null: " . (NAMESPACE_CONST_NULL === null ? 'null' : 'not null') . "\n";
    echo "Array: " . implode(',', NAMESPACE_CONST_ARRAY) . "\n";

    // Test define() constants
    echo "\n=== Define Constants ===\n";
    echo "String: " . DEFINE_STRING . "\n";
    echo "Int: " . DEFINE_INT . "\n";
    echo "Float: " . DEFINE_FLOAT . "\n";
    echo "Bool: " . (DEFINE_BOOL ? 'true' : 'false') . "\n";
    echo "Null: " . (DEFINE_NULL === null ? 'null' : 'not null') . "\n";
    echo "Array count: " . count(DEFINE_ARRAY) . "\n";

    // Test class constants
    echo "\n=== Class Constants ===\n";
    $demo = new ConstantsDemo();
    $constants = $demo::getConstants();
    echo "Public: " . $constants['public'] . "\n";
    echo "Protected: " . $constants['protected'] . "\n";
    echo "Private: " . $constants['private'] . "\n";
    echo "Default: " . $constants['default'] . "\n";
    echo "Public Int: " . $constants['public_int'] . "\n";
    echo "Protected Int: " . $constants['protected_int'] . "\n";
    echo "Private Int: " . $constants['private_int'] . "\n";
    echo "Public Array: " . implode(',', $constants['public_array']) . "\n";

    // Test interface constants
    echo "\n=== Interface Constants ===\n";
    echo "Interface const: " . IConstants::INTERFACE_CONST . "\n";
    echo "Class with interface: " . ClassWithInterface::INTERFACE_CONST . "\n";

    // Test trait constants
    echo "\n=== Trait Constants ===\n";
    echo "Trait const: " . ClassWithTrait::TRAIT_CONST . "\n";

    return 0;
}
PHP;

file_put_contents($src, $code);

if (opkit_compile_file(__DIR__, $src)) {
    if (opkit_load($output)) {
        opkit_boot();
    } else {
        echo "Failed to load\n";
    }
} else {
    echo "Failed to compile\n";
}

@unlink($src);
@unlink($output);
?>
--EXPECT--
=== Namespace Constants ===
String: namespace const value
Int: 999
Float: 3.14159
Bool: true
Null: null
Array: value1,1,2,3

=== Define Constants ===
String: defined string
Int: 42
Float: 2.71828
Bool: false
Null: null
Array count: 4

=== Class Constants ===
Public: public constant
Protected: protected constant
Private: private constant
Default: default visibility constant
Public Int: 100
Protected Int: 200
Private Int: 300
Public Array: 1,2,3

=== Interface Constants ===
Interface const: interface constant
Class with interface: interface constant

=== Trait Constants ===
Trait const: trait constant