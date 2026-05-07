--TEST--
OpKit: Shared memory loading with opkit.shm_size
--EXTENSIONS--
opkit
--INI--
opkit.shm_size=33554432
--FILE--
<?php
$src = sys_get_temp_dir() . "/test_shm_src.php";
$dst = sys_get_temp_dir() . "/test_shm_out";
file_put_contents($src, "<?php\nfunction hello_shm() { return 'world_from_shm'; }\nfunction main() { return 0; }\n");
@mkdir($dst, 0777, true);

$ok = opkit_compile_file($dst, $src);
if (!$ok) {
    echo "COMPILE_FAIL\n";
    exit(1);
}

$phpc = $dst . "/test_shm_src.phpc";
$ok = opkit_load($phpc);
if (!$ok) {
    echo "LOAD_FAIL\n";
    exit(1);
}

$result = opkit_boot();
if ($result !== 0) {
    echo "BOOT_FAIL\n";
    exit(1);
}

echo hello_shm() . "\n";

/* Verify shared memory segment exists in process maps */
$maps = file_get_contents("/proc/self/maps");
if (preg_match("/rw-s\s+/", $maps)) {
    echo "SHM_OK\n";
}
?>
--EXPECT--
world_from_shm
SHM_OK
