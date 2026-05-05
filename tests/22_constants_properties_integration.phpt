--TEST--
OpKit: Integration test for constants and properties (inheritance, edge cases)
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . '/22_constants_properties_integration_src.php';
$output = __DIR__ . '/22_constants_properties_integration_src.phpc';

$code = <<<'PHP'
<?php

// Constants used in property default values
const DEFAULT_NAME = 'Default User';
const DEFAULT_AGE = 25;

// Abstract class with constants and properties
abstract class BaseEntity {
    public const PUBLIC_CONST = 'public from base';
    protected const PROTECTED_CONST = 'protected from base';

    public string $id;
    protected string $createdAt;

    public function __construct() {
        $this->id = 'entity_test123';
        $this->createdAt = '2025-03-23';
    }

    public function getProtectedConst(): string {
        return static::PROTECTED_CONST;
    }

    public function getCreatedAt(): string {
        return $this->createdAt;
    }
}

// Concrete class inheriting constants and properties
class User extends BaseEntity {
    public const USER_CONST = 'user specific';

    public string $name;
    public int $age;
    public static int $userCount = 0;

    private array $metadata = [];

    public function __construct(string $name = DEFAULT_NAME, int $age = DEFAULT_AGE) {
        parent::__construct();
        $this->name = $name;
        $this->age = $age;
        self::$userCount++;
    }

    public function setMetadata(string $key, $value): void {
        $this->metadata[$key] = $value;
    }

    public function getMetadata(string $key) {
        return $this->metadata[$key] ?? null;
    }

    public function getInfo(): string {
        return "{$this->name} ({$this->age}) created at {$this->createdAt}";
    }
}

// Class using constant in property default
class Configurable {
    public string $configValue = DEFAULT_NAME;
    public int $configNumber = DEFAULT_AGE;

    public function getConfig(): string {
        return "{$this->configValue}: {$this->configNumber}";
    }
}

// Enum-like constants class
class StatusCodes {
    public const OK = 200;
    public const NOT_FOUND = 404;
    public const ERROR = 500;

    private const MESSAGES = [
        self::OK => 'OK',
        self::NOT_FOUND => 'Not Found',
        self::ERROR => 'Server Error',
    ];

    public static function getMessage(int $code): string {
        return self::MESSAGES[$code] ?? 'Unknown';
    }
}

// Final class with private properties
final class ImmutableSettings {
    private string $setting1;
    private int $setting2;

    public function __construct(string $s1, int $s2) {
        $this->setting1 = $s1;
        $this->setting2 = $s2;
    }

    public function getSetting1(): string {
        return $this->setting1;
    }

    public function getSetting2(): int {
        return $this->setting2;
    }
}

function main() {
    echo "=== Inheritance and Constants ===\n";
    $user = new User();
    echo "User ID starts with: " . substr($user->id, 0, 7) . "\n";
    echo "Public const: " . User::PUBLIC_CONST . "\n";
    echo "Protected const via method: " . $user->getProtectedConst() . "\n";
    echo "User const: " . User::USER_CONST . "\n";
    echo "Default name const: " . DEFAULT_NAME . "\n";
    echo "Default age const: " . DEFAULT_AGE . "\n";

    echo "\n=== Properties ===\n";
    echo "Name default: " . $user->name . "\n";
    echo "Age default: " . $user->age . "\n";
    echo "Created at: " . $user->getCreatedAt() . "\n";
    echo "User count: " . User::$userCount . "\n";
    echo "Info: " . $user->getInfo() . "\n";

    echo "\n=== Metadata (dynamic properties) ===\n";
    $user->setMetadata('role', 'admin');
    $user->setMetadata('active', true);
    echo "Role: " . $user->getMetadata('role') . "\n";
    echo "Active: " . ($user->getMetadata('active') ? 'yes' : 'no') . "\n";
    echo "Missing: " . ($user->getMetadata('missing') === null ? 'null' : 'not null') . "\n";

    echo "\n=== Config with constant defaults ===\n";
    $config = new Configurable();
    echo "Config: " . $config->getConfig() . "\n";

    echo "\n=== Status Codes ===\n";
    echo "OK message: " . StatusCodes::getMessage(200) . "\n";
    echo "404 message: " . StatusCodes::getMessage(404) . "\n";
    echo "500 message: " . StatusCodes::getMessage(500) . "\n";

    echo "\n=== Final Class ===\n";
    $settings = new ImmutableSettings('test', 123);
    echo "Setting1: " . $settings->getSetting1() . "\n";
    echo "Setting2: " . $settings->getSetting2() . "\n";

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
--EXPECTF--
=== Inheritance and Constants ===
User ID starts with: entity_
Public const: public from base
Protected const via method: protected from base
User const: user specific
Default name const: Default User
Default age const: 25

=== Properties ===
Name default: Default User
Age default: 25
Created at: 2025-03-23
User count: 1
Info: Default User (25) created at 2025-03-23

=== Metadata (dynamic properties) ===
Role: admin
Active: yes
Missing: null

=== Config with constant defaults ===
Config: Default User: 25

=== Status Codes ===
OK message: OK
404 message: Not Found
500 message: Server Error

=== Final Class ===
Setting1: test
Setting2: 123
