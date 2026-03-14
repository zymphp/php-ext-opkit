--TEST--
OpKit: Phar transparent loading
--EXTENSIONS--
opkit
phar
--INI--
phar.readonly=0
--FILE--
<?php
$base_dir = __DIR__ . "/test_04_src";
@mkdir($base_dir, 0777, true);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main() {
    echo "Hello from OpKit inside a Phar!\n";
    echo "Phar running: [" . \Phar::running() . "]\n";
    return 0;
}
PHP
);

$output_dir = __DIR__ . "/test_04_target";
@mkdir($output_dir, 0777, true);

if (opkit_compile_dir($output_dir, $base_dir)) {
    opkit_gen_entry_file($output_dir . "/entry.php");

    $phar_file = __DIR__ . "/test_04_app.phar";
    if (file_exists($phar_file)) unlink($phar_file);

    $phar = new Phar($phar_file);
    $phar->buildFromDirectory($output_dir);
    $phar->setStub($phar->createDefaultStub('entry.php'));
    unset($phar);

    include $phar_file;
}

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
if (file_exists($phar_file)) unlink($phar_file);
?>
--EXPECTF--
Hello from OpKit inside a Phar!
Phar running: [%s/test_04_app.phar]
