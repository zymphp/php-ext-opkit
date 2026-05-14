--TEST--
OpKit: Enum support (backed/unbacked, default values)
--EXTENSIONS--
opkit
--FILE--
<?php
$base_dir = __DIR__ . "/test_33_enum_src";
$output_dir = __DIR__ . "/test_33_enum_out";
@mkdir($base_dir, 0777, true);
@mkdir($output_dir, 0777, true);

file_put_contents($base_dir . "/test.php", <<<'PHP'
<?php

enum Status: string {
    case ACTIVE = 'active';
    case INACTIVE = 'inactive';
    case PENDING = 'pending';
}

enum Priority: int {
    case LOW = 1;
    case MEDIUM = 5;
    case HIGH = 10;
}

function main(): int {
    var_dump(Status::ACTIVE->value);
    var_dump(Status::INACTIVE->name);
    var_dump(Status::PENDING->value);

    var_dump(Status::cases());

    var_dump(Status::from('inactive'));
    var_dump(Status::tryFrom('nonexistent'));

    var_dump(Priority::HIGH->value);
    var_dump(Priority::from(1));
    var_dump(Priority::LOW->value);
    var_dump(Priority::from(5)->name);

    echo "enum test ok\n";
    return 0;
}
PHP
);

$r = opkit_compile_file($output_dir, $base_dir . "/test.php", $base_dir);
var_dump($r);

opkit_load($output_dir . "/test.phpc");
opkit_boot();

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
--EXPECTF--
bool(true)
string(6) "active"
string(8) "INACTIVE"
string(7) "pending"
array(3) {
  [0]=>
  enum(Status::ACTIVE)
  [1]=>
  enum(Status::INACTIVE)
  [2]=>
  enum(Status::PENDING)
}
enum(Status::INACTIVE)
NULL
int(10)
enum(Priority::LOW)
int(1)
string(6) "MEDIUM"
enum test ok
%A
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
@rmrf(__DIR__ . "/test_33_enum_src");
@rmrf(__DIR__ . "/test_33_enum_out");
?>
