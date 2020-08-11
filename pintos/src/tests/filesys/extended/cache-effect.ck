# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-effect) begin
(cache-effect) bytes_read 239
(cache-effect) buf_size 239
(cache-effect) bytes_read 239
(cache-effect) buf_size 239
(cache-effect) end
EOF
pass;