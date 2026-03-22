--TEST--
OpKit: Comprehensive class properties support (typed, visibility, static, readonly, defaults)
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . '/21_properties_comprehensive_src.php';
$output = __DIR__ . '/21_properties_comprehensive_src.phpc';

$code = <<<'PHP'
<?php

class PropertiesDemo {
    // Typed properties with default values
    public string $typedString = 'default string';
    public int $typedInt = 100;
    public float $typedFloat = 1.5;
    public bool $typedBool = true;
    public array $typedArray = [1, 2, 3];
    public ?string $nullableString = null;

    // Visibility variations
    private string $privateProp = 'private value';
    protected int $protectedProp = 42;
    public string $publicProp = 'public value';

    // Static properties
    public static string $staticPublic = 'static public';
    protected static int $staticProtected = 999;
    private static array $staticPrivate = ['static', 'private'];

    // Untyped properties (PHP < 7.4 style)
    public $untypedProp = 'untyped';
    private $untypedPrivate = 123;

    // Property without default (initialized in constructor)
    public string $constructorInit;

    public function __construct() {
        $this->constructorInit = 'initialized in constructor';
    }

    public function getPrivate(): string {
        return $this->privateProp;
    }

    public function getProtected(): int {
        return $this->protectedProp;
    }

    public function getUntypedPrivate() {
        return $this->untypedPrivate;
    }

    public static function getStaticProtected(): int {
        return self::$staticProtected;
    }

    public static function getStaticPrivate(): array {
        return self::$staticPrivate;
    }
}

// Readonly properties (PHP 8.1+)
class ReadonlyDemo {
    public readonly string $readonlyString;
    public readonly int $readonlyInt;

    public function __construct() {
        $this->readonlyString = 'readonly value';
        $this->readonlyInt = 42;
    }

    public function getReadonlyString(): string {
        return $this->readonlyString;
    }

    public function getReadonlyInt(): int {
        return $this->readonlyInt;
    }
}

// Complex types - union and intersection (PHP 8.0+)
class UnionTypesDemo {
    public string|int $unionProp = 'union as string';
    public array|null $nullableArray = null;

    public function getUnionAsInt(): int {
        $this->unionProp = 999;
        return $this->unionProp;
    }
}

// Property initialization with complex expressions
class ComplexDefaultsDemo {
    public array $indexedArray;
    public string $concatenated;

    public function __construct() {
        $this->indexedArray = range(1, 5);
        $this->concatenated = 'Hello' . ' ' . 'World';
    }

    public function getArray(): array {
        return $this->indexedArray;
    }

    public function getConcatenated(): string {
        return $this->concatenated;
    }
}

function main() {
    // Test typed and visibility properties
    echo "=== Typed and Visibility Properties ===\n";
    $demo = new PropertiesDemo();
    echo "Typed string: " . $demo->typedString . "\n";
    echo "Typed int: " . $demo->typedInt . "\n";
    echo "Typed float: " . $demo->typedFloat . "\n";
    echo "Typed bool: " . ($demo->typedBool ? 'true' : 'false') . "\n";
    echo "Typed array: " . implode(',', $demo->typedArray) . "\n";
    echo "Nullable (null): " . ($demo->nullableString === null ? 'yes' : 'no') . "\n";

    echo "Private via method: " . $demo->getPrivate() . "\n";
    echo "Protected via method: " . $demo->getProtected() . "\n";
    echo "Public: " . $demo->publicProp . "\n";
    echo "Untyped: " . $demo->untypedProp . "\n";
    echo "Untyped private: " . $demo->getUntypedPrivate() . "\n";
    echo "Constructor init: " . $demo->constructorInit . "\n";

    // Test static properties
    echo "\n=== Static Properties ===\n";
    echo "Static public: " . PropertiesDemo::$staticPublic . "\n";
    echo "Static protected via method: " . PropertiesDemo::getStaticProtected() . "\n";
    echo "Static private via method: " . implode(',', PropertiesDemo::getStaticPrivate()) . "\n";

    // Test readonly properties
    echo "\n=== Readonly Properties ===\n";
    $readonly = new ReadonlyDemo();
    echo "Readonly string: " . $readonly->getReadonlyString() . "\n";
    echo "Readonly int: " . $readonly->getReadonlyInt() . "\n";

    // Test union types
    echo "\n=== Union Types ===\n";
    $union = new UnionTypesDemo();
    echo "Union as string: " . $union->unionProp . "\n";
    echo "Union as int: " . $union->getUnionAsInt() . "\n";
    echo "Nullable array is null: " . ($union->nullableArray === null ? 'yes' : 'no') . "\n";

    // Test complex defaults
    echo "\n=== Complex Defaults ===\n";
    $complex = new ComplexDefaultsDemo();
    echo "Indexed array: " . implode(',', $complex->getArray()) . "\n";
    echo "Concatenated: " . $complex->getConcatenated() . "\n";

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
=== Typed and Visibility Properties ===
Typed string: default string
Typed int: 100
Typed float: 1.5
Typed bool: true
Typed array: 1,2,3
Nullable (null): yes
Private via method: private value
Protected via method: 42
Public: public value
Untyped: untyped
Untyped private: 123
Constructor init: initialized in constructor

=== Static Properties ===
Static public: static public
Static protected via method: 999
Static private via method: static,private

=== Readonly Properties ===
Readonly string: readonly value
Readonly int: 42

=== Union Types ===
Union as string: union as string
Union as int: 999
Nullable array is null: yes

=== Complex Defaults ===
Indexed array: 1,2,3,4,5
Concatenated: Hello World