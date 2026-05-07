--TEST--
OpKit: Fork shared memory verification
--SKIPIF--
<?php if (!extension_loaded('pcntl')) die('skip pcntl extension required'); ?>
--EXTENSIONS--
opkit
--INI--
opkit.shm_size=33554432
--FILE--
<?php
$src = sys_get_temp_dir() . "/test_fork_src.php";
$dst = sys_get_temp_dir() . "/test_fork_out";
file_put_contents($src, "<?php\nfunction fork_hello() { return 'from_fork'; }\nfunction main() { return 0; }\n");
@mkdir($dst, 0777, true);

// Compile and load into parent process SHM
$ok = opkit_compile_file($dst, $src);
if (!$ok) {
    echo "COMPILE_FAIL\n";
    exit(1);
}
$phpc = $dst . "/test_fork_src.phpc";
$ok = opkit_load($phpc);
if (!$ok) {
    echo "LOAD_FAIL\n";
    exit(1);
}

// Boot in parent so classes/functions are registered
opkit_boot();

$pid = pcntl_fork();
if ($pid === -1) {
    echo "FORK_FAIL\n";
    exit(1);
} elseif ($pid === 0) {
    // Child process: should directly access parent-loaded SHM data
    if (function_exists('fork_hello')) {
        echo "CHILD_FUNC_OK:" . fork_hello() . "\n";
    } else {
        echo "CHILD_FUNC_MISSING\n";
    }
    exit(0);
} else {
    pcntl_waitpid($pid, $status);
    echo "PARENT_WAIT_DONE\n";
}
?>
--EXPECTF--
CHILD_FUNC_OK:from_fork
PARENT_WAIT_DONE
