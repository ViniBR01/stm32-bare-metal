#include "bpsk.h"

void bpsk_map_block(const uint8_t *bits, q15_t *syms, size_t n)
{
    if (bits == NULL || syms == NULL) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        syms[i] = bpsk_map(bits[i]);
    }
}

void bpsk_slice_block(const q15_t *samples, uint8_t *bits, size_t n)
{
    if (samples == NULL || bits == NULL) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        bits[i] = bpsk_slice(samples[i]);
    }
}
