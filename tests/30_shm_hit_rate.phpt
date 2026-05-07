--TEST--
OpKit: SHM hit rate and memory usage comparison with opkit_shm_reset
--EXTENSIONS--
opkit
--INI--
opkit.shm_size=33554432
--FILE--
<?php
$src = sys_get_temp_dir() . "/test_hitrate_src.php";
$dst = sys_get_temp_dir() . "/test_hitrate_out";

// Generate a moderately sized PHP file
$code = "<?php\n";
for ($i = 0; $i < 500; $i++) {
    $code .= "function hitrate_func_$i() { return " . ($i * 2) . "; }\n";
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
$phpc = $dst . "/test_hitrate_src.phpc";

// Baseline heap
$baseline = memory_get_usage(true);

// First load cycle
$ok1 = opkit_load($phpc);
if (!$ok1) {
    echo "LOAD1_FAIL\n";
    exit(1);
}
$stat1 = opkit_shm_stat();
$used1 = $stat1['used'];
$heap1 = memory_get_usage(true);

// Second load of the same file (current allocator does not deduplicate,
// so SHM will increase, but heap growth should be minimal)
$ok2 = opkit_load($phpc);
if (!$ok2) {
    echo "LOAD2_FAIL\n";
    exit(1);
}
$stat2 = opkit_shm_stat();
$used2 = $stat2['used'];
$heap2 = memory_get_usage(true);

// Boot
$result = opkit_boot();
if ($result !== 0) {
    echo "BOOT_FAIL\n";
    exit(1);
}

// Verify SHM was used
if ($used1 <= 0) {
    echo "SHM_NOT_USED\n";
    exit(1);
}

// Current allocator allocates fresh SHM on each load, so used2 should be roughly 2x used1
if ($used2 < $used1) {
    echo "SHM_UNEXPECTED:$used1,$used2\n";
    exit(1);
}
echo "SHM_USED1:$used1\n";
echo "SHM_USED2:$used2\n";

// Heap growth should be minimal across duplicate loads (bookkeeping only)
$heap_growth = $heap2 - $heap1;
if ($heap_growth > 50000) {
    echo "HEAP_HIGH:$heap_growth\n";
    exit(1);
}
echo "HEAP_OK:$heap_growth\n";

// Reset and measure again
$reset_ok = opkit_shm_reset();
if (!$reset_ok) {
    echo "RESET_FAIL\n";
    exit(1);
}

$stat_after_reset = opkit_shm_stat();
if ($stat_after_reset['used'] != 0) {
    echo "RESET_NOT_ZERO:" . $stat_after_reset['used'] . "\n";
    exit(1);
}
echo "RESET_ZERO_OK\n";

// Reload after reset
$ok3 = opkit_load($phpc);
if (!$ok3) {
    echo "LOAD3_FAIL\n";
    exit(1);
}
$stat3 = opkit_shm_stat();
$used3 = $stat3['used'];

// SHM usage after reload should be similar to initial load
$diff = abs($used3 - $used1);
$ratio = $used1 > 0 ? ($diff / $used1) : 0;
if ($ratio > 0.1) {
    echo "RELOAD_SIZE_MISMATCH:$used1,$used3\n";
    exit(1);
}
echo "RELOAD_SIZE_MATCH:$used1,$used3\n";

// Boot after reload
$result2 = opkit_boot();
if ($result2 !== 0) {
    echo "REBOOT_FAIL\n";
    exit(1);
}
echo "BOOT_OK\n";

// Verify a function is callable after reload
if (function_exists('hitrate_func_123')) {
    $val = hitrate_func_123();
    if ($val === 246) {
        echo "FUNC_OK\n";
    } else {
        echo "FUNC_WRONG:$val\n";
    }
} else {
    echo "FUNC_MISSING\n";
}
?>
--EXPECTF--
SHM_USED1:%d
SHM_USED2:%d
HEAP_OK:%d
RESET_ZERO_OK
RELOAD_SIZE_MATCH:%d,%d
BOOT_OK
FUNC_OK
