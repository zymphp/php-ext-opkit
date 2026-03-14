--TEST--
OpKit: Relative path and Phar support
--EXTENSIONS--
opkit
phar
--INI--
phar.readonly=0
--FILE--
<?php
$base_dir = __DIR__ . "/test_03_src";
@mkdir($base_dir, 0777, true);
@mkdir($base_dir . "/app/core", 0777, true);

file_put_contents($base_dir . "/app/core/Application.php", <<<'PHP'
<?php
namespace App\Core;
class Application {
    public function run() {
        echo "Phar running: [" . \Phar::running() . "]\n";
        // 过滤掉绝对路径部分，只保留 phar://...app.phar/app/core/Application.php
        $file = __FILE__;
        if (strpos($file, 'phar://') === 0) {
            echo "Current file: " . substr($file, strrpos($file, 'app.phar')) . "\n";
        } else {
            echo "Current file: " . $file . "\n";
        }
        echo "Application is running from namespace!\n";
    }
}
PHP
);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main() {
    $app = new \App\Core\Application();
    $app->run();
    return 0;
}
PHP
);

$output_dir = __DIR__ . "/test_03_target";
@mkdir($output_dir, 0777, true);

if (opkit_compile_dir($output_dir, $base_dir)) {
    opkit_gen_entry_file($output_dir . "/entry.php");

    $phar_file = __DIR__ . "/app.phar";
    if (file_exists($phar_file)) unlink($phar_file);

    $phar = new Phar($phar_file);
    $phar->buildFromDirectory($output_dir);
    $phar->setStub($phar->createDefaultStub('entry.php'));
    unset($phar);

    // 运行 Phar
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
Phar running: [%s/app.phar]
Current file: app.phar/app/core/Application.php
Application is running from namespace!
