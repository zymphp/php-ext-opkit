--TEST--
OpKit: TripleDES encryption and decryption
--EXTENSIONS--
opkit
--SKIPIF--
<?php if (!extension_loaded("openssl")) print "skip openssl extension not loaded"; ?>
--FILE--
<?php
$base_dir = __DIR__ . "/test_05_src";
@mkdir($base_dir);

file_put_contents($base_dir . "/TripleDesEcb.php", <<<'PHP'
<?php
class TripleDesEcb {
    public function encrypt($plaintext, $key) {
        return base64_encode(openssl_encrypt($plaintext, 'DES-EDE3', $key, OPENSSL_RAW_DATA));
    }
    public function decrypt($ciphertext, $key) {
        return openssl_decrypt(base64_decode($ciphertext), 'DES-EDE3', $key, OPENSSL_RAW_DATA);
    }
}
PHP
);

file_put_contents($base_dir . "/main.php", <<<'PHP'
<?php
function main() {
    $c = new TripleDesEcb();
    $plaintext = "Hello OpKit!";
    $key = "123456789012345678901234";
    $enc = $c->encrypt($plaintext, $key);
    $dec = $c->decrypt($enc, $key);
    echo $dec . "\n";
    return 0;
}
PHP
);

$output_dir = __DIR__ . "/test_05_target";
@mkdir($output_dir);

if (opkit_compile_dir($output_dir, $base_dir)) {
    opkit_load($output_dir . "/TripleDesEcb.phpc");
    opkit_load($output_dir . "/main.phpc");
    opkit_boot();
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
?>
--EXPECT--
Hello OpKit!
