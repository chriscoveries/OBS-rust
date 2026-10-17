/* Forwarding file (BUILD.md): the test/cmocka/test_vt_*.c tests include
 * this source by its old name. The build compiles encoder.c.h through its
 * unity group, never this file. */
#include "encoder.c.h"
