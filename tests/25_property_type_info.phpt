--TEST--
OpKit: Property type info in opkit_get_info metadata
--EXTENSIONS--
opkit
--FILE--
<?php
$src = __DIR__ . '/25_property_type_info_src.php';
$output = __DIR__ . '/25_property_type_info_src.phpc';

$code = <<<'PHP'
<?php
class TypeInfoTest {
    public string $typedString = 'hello';
    public int $typedInt = 42;
    public ?string $nullable = null;
    public static array $staticArray = [];
    public $untyped = 'no type';
}
PHP;

file_put_contents($src, $code);

if (opkit_compile_file(__DIR__, $src)) {
    $info = opkit_get_info($output);
    if ($info && !empty($info['classes'])) {
        foreach ($info['classes'] as $class) {
            if ($class['name'] === 'TypeInfoTest') {
                foreach ($class['properties'] as $prop) {
                    $type = $prop['type'] ?? 'none';
                    echo $prop['name'] . ':' . $type . "\n";
                }
            }
        }
    } else {
        echo "No class info\n";
    }
} else {
    echo "Failed to compile\n";
}

@unlink($src);
@unlink($output);
?>
--EXPECT--
typedString:string
typedInt:int
nullable:string|null
staticArray:array
untyped:none
