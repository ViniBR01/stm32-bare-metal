/*
 * Host-test stub for 3rd_party/printf/printf.h
 *
 * The embedded build maps printf -> printf_ (mpaland implementation).
 * For host tests we skip that remapping and let cli.c call the standard
 * libc printf directly, which avoids pulling in the entire embedded printf
 * library and its ARM-specific linkage requirements.
 */
#pragma once
#include <stdio.h>
