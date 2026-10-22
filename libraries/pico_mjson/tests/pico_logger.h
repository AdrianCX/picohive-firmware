// Stub for the host tests: the firmware logger is not built here.
#pragma once

#include <stdio.h>

#define trace(...) do { printf("    log: " __VA_ARGS__); printf("\n"); } while (0)
