--TEST--
OpKit: opkit_shm_reset() with fork shared memory verification
--SKIPIF--
<?php if (!extension_loaded('pcntl')) die('skip pcntl extension required'); ?>
--EXTENSIONS--
opkit
--INI--
opkit.shm_size=33554432
--FILE--
<?php
$src = sys_get_temp_dir() . "/test_reset_fork_src.php";
$dst = sys_get_temp_dir() . "/test_reset_fork_out";
file_put_contents($src, "<?php\nfunction reset_fork_hello() { return 'from_reset_fork'; }\nfunction main() { return 0; }\n");
@mkdir($dst, 0777, true);

// Phase 1: Compile and load into parent process SHM
$ok = opkit_compile_file($dst, $src);
if (!$ok) {
    echo "COMPILE_FAIL\n";
    exit(1);
}
$phpc = $dst . "/test_reset_fork_src.phpc";
$ok = opkit_load($phpc);
if (!$ok) {
    echo "LOAD_FAIL\n";
    exit(1);
}
opkit_boot();

// Phase 2: Fork child to verify initial shared access
$pid = pcntl_fork();
if ($pid === -1) {
    echo "FORK_FAIL\n";
    exit(1);
} elseif ($pid === 0) {
    if (function_exists('reset_fork_hello')) {
        echo "CHILD_INITIAL:" . reset_fork_hello() . "\n";
    } else {
        echo "CHILD_INITIAL_MISSING\n";
    }
    exit(0);
} else {
    pcntl_waitpid($pid, $status);
}

// Phase 3: Reset SHM and reload
$reset_ok = opkit_shm_reset();
if (!$reset_ok) {
    echo "RESET_FAIL\n";
    exit(1);
}

// After reset, opkit_is_loaded should return false
if (opkit_is_loaded($phpc)) {
    echo "IS_LOADED_AFTER_RESET\n";
    exit(1);
}

// Reload the same file
$ok = opkit_load($phpc);
if (!$ok) {
    echo "RELOAD_FAIL\n";
    exit(1);
}

// Boot again after reload
$result = opkit_boot();
if ($result !== 0) {
    echo "REBOOT_FAIL\n";
    exit(1);
}

// Phase 4: Fork another child to verify shared access after reset+reload
$pid2 = pcntl_fork();
if ($pid2 === -1) {
    echo "FORK2_FAIL\n";
    exit(1);
} elseif ($pid2 === 0) {
    if (function_exists('reset_fork_hello')) {
        echo "CHILD_AFTER_RESET:" . reset_fork_hello() . "\n";
    } else {
        echo "CHILD_AFTER_RESET_MISSING\n";
    }
    exit(0);
} else {
    pcntl_waitpid($pid2, $status2);
}

// Phase 5: Verify parent still works
if (function_exists('reset_fork_hello')) {
    echo "PARENT_AFTER_RESET:" . reset_fork_hello() . "\n";
} else {
    echo "PARENT_AFTER_RESET_MISSING\n";
}

// Phase 6: Verify SHM stat is reasonable after reset
$stat = opkit_shm_stat();
if ($stat === null) {
    echo "STAT_NULL\n";
    exit(1);
}
if ($stat['used'] > 0 && $stat['free'] > 0) {
    echo "STAT_OK\n";
} else {
    echo "STAT_BAD\n";
    exit(1);
}
?>
--EXPECT--
CHILD_INITIAL:from_reset_fork
CHILD_AFTER_RESET:from_reset_fork
PARENT_AFTER_RESET:from_reset_fork
STAT_OK
