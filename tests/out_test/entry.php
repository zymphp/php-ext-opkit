<?php

if (!extension_loaded('opkit')) {
    if (!@dl('opkit.so')) {
        trigger_error('OpKit extension not loaded', E_USER_ERROR);
    }
}

opkit_load_multi([
    __DIR__ . '/test_no_main.phpc',
]);

exit(opkit_boot());
