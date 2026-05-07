--TEST--
OpKit: SHM memory usage comparison
--EXTENSIONS--
opkit
--INI--
opkit.shm_size=33554432
--FILE--
<?php
$src = sys_get_temp_dir() . "/test_mem_src.php";
$dst = sys_get_temp_dir() . "/test_mem_out";

// Generate a moderately sized PHP file
$code = "<?php\n";
for ($i = 0; $i < 500; $i++) {
    $code .= "function func_$i() { return \$i * 2; }\n";
}
$code .= "function main() { return 0; }\n";
file_put_contents($src, $code);
@mkdir($dst, 0777, true);

// Compile once
$ok = opkit_compile_file($dst, $src);
if (!$ok) {
    echo "COMPILE_FAIL\n";
    exit(1);
}
$phpc = $dst . "/test_mem_src.phpc";

// Get baseline memory
$baseline = memory_get_usage(true);

// Load 10 times (should reuse SHM or allocate each time)
for ($i = 0; $i < 10; $i++) {
    opkit_load($phpc);
}

$after_load = memory_get_usage(true);
$heap_increase = $after_load - $baseline;

$stat = opkit_shm_stat();
$shm_used = $stat['used'];

// Verify SHM was actually used (should have consumed some SHM)
if ($shm_used > 0) {
    echo "SHM_USED:$shm_used\n";
} else {
    echo "SHM_NOT_USED\n";
}

// Heap should not grow proportionally to 10 loads because data is in SHM
// Allow some tolerance for node bookkeeping
if ($heap_increase < 50000) {
    echo "HEAP_OK:$heap_increase\n";
} else {
    echo "HEAP_HIGH:$heap_increase\n";
}

// Verify boot still works after multiple loads
$result = opkit_boot();
if ($result === 0) {
    echo "BOOT_OK\n";
} else {
    echo "BOOT_FAIL\n";
}
?>
--EXPECTF--
SHM_USED:%d
HEAP_OK:%d
BOOT_OK
