#ifndef LIB_IMG_HEADER_H
#define LIB_IMG_HEADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * On-flash image header and slot metadata format. These structs are written
 * verbatim by the host signing tool and parsed verbatim by the bootloader, so
 * their layout must be deterministic across host gcc and arm-none-eabi-gcc.
 *
 * `__attribute__((packed))` is used so no implicit padding is ever inserted,
 * even if a future field is added with a non-aligned size. All current fields
 * are naturally 4-byte aligned, so there is no unaligned-access penalty on
 * Cortex-M4. Sizes are also pinned with _Static_assert below.
 */

/* "IMGH" in ASCII, little-endian. */
#define IMG_HEADER_MAGIC          0x494D4748u

/* "SLOT" in ASCII, little-endian. */
#define IMG_SLOT_METADATA_MAGIC   0x534C4F54u

/* Header format version. Bump on incompatible changes. */
#define IMG_HEADER_VERSION        1u
#define IMG_SLOT_METADATA_VERSION 1u

/* Image type enum values. Stored as a uint32_t in the header. */
#define IMG_TYPE_BOOTLOADER       1u
#define IMG_TYPE_APP              2u

#define IMG_SHA256_SIZE           32
#define IMG_SIGNATURE_SIZE        64  /* ECDSA P-256 raw R||S, 32 + 32 bytes. */

typedef struct __attribute__((packed)) {
    uint32_t magic;            /* IMG_HEADER_MAGIC */
    uint32_t header_version;   /* IMG_HEADER_VERSION */
    uint32_t image_version;    /* monotonic firmware version (anti-rollback) */
    uint32_t image_type;       /* IMG_TYPE_BOOTLOADER | IMG_TYPE_APP */
    uint32_t payload_size;     /* bytes of payload following the header */
    uint32_t payload_offset;   /* bytes from header start to payload start */
    uint8_t  sha256[IMG_SHA256_SIZE];
    uint8_t  signature[IMG_SIGNATURE_SIZE];
    uint32_t reserved[4];
    uint32_t header_crc;       /* CRC-32 over all preceding header bytes */
} img_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* IMG_SLOT_METADATA_MAGIC */
    uint32_t metadata_version;  /* IMG_SLOT_METADATA_VERSION */
    uint32_t active;            /* non-zero = active slot */
    uint32_t fail_count;        /* bootloader increments before jump */
    uint32_t monotonic_counter; /* anti-rollback floor */
    uint32_t reserved[3];
    uint32_t metadata_crc;      /* CRC-32 over all preceding bytes */
} img_slot_metadata_t;

/*
 * Pin the on-flash sizes. If any future change to the structs perturbs the
 * layout, these asserts fail at compile time on both host and target.
 *   header  : 6*4 + 32 + 64 + 4*4 + 4 = 140 bytes
 *   metadata: 5*4 + 3*4 + 4         =  36 bytes
 */
_Static_assert(sizeof(img_header_t) == 140,
               "img_header_t must be exactly 140 bytes on flash");
_Static_assert(sizeof(img_slot_metadata_t) == 36,
               "img_slot_metadata_t must be exactly 36 bytes on flash");

typedef enum {
    IMG_OK              = 0,
    IMG_ERR_BAD_MAGIC   = -1,
    IMG_ERR_BAD_VERSION = -2,
    IMG_ERR_BAD_TYPE    = -3,
    IMG_ERR_BAD_SIZE    = -4,
    IMG_ERR_BAD_OFFSET  = -5,
    IMG_ERR_BAD_CRC     = -6,
    IMG_ERR_NULL_ARG    = -7,
} img_err_t;

/*
 * Parse and validate an image header from a raw byte buffer.
 *
 * Validation order (first failure wins):
 *   1. NULL args            -> IMG_ERR_NULL_ARG
 *   2. buf_len too small    -> IMG_ERR_BAD_SIZE
 *   3. CRC mismatch         -> IMG_ERR_BAD_CRC
 *   4. Magic mismatch       -> IMG_ERR_BAD_MAGIC
 *   5. header_version != 1  -> IMG_ERR_BAD_VERSION
 *   6. unknown image_type   -> IMG_ERR_BAD_TYPE
 *   7. payload_offset < sizeof(header)  -> IMG_ERR_BAD_OFFSET
 *      payload_size == 0                -> IMG_ERR_BAD_SIZE
 *
 * On success the validated struct is copied into *out.
 */
img_err_t img_header_parse(const uint8_t *buf, size_t buf_len, img_header_t *out);

/*
 * Parse and validate a slot metadata blob from a raw byte buffer.
 *
 * Validation order (first failure wins):
 *   1. NULL args              -> IMG_ERR_NULL_ARG
 *   2. buf_len too small      -> IMG_ERR_BAD_SIZE
 *   3. CRC mismatch           -> IMG_ERR_BAD_CRC
 *   4. Magic mismatch         -> IMG_ERR_BAD_MAGIC
 *   5. metadata_version != 1  -> IMG_ERR_BAD_VERSION
 *
 * On success the validated struct is copied into *out.
 */
img_err_t img_slot_metadata_parse(const uint8_t *buf, size_t buf_len,
                                  img_slot_metadata_t *out);

/*
 * Standard IEEE 802.3 / zlib CRC-32. Polynomial 0xEDB88320 (reflected),
 * initial value 0xFFFFFFFF, reflected input/output, final XOR 0xFFFFFFFF.
 * Software-only — does not use the STM32 hardware CRC engine, so the host
 * signer can match without special hardware.
 */
uint32_t img_crc32(const uint8_t *buf, size_t len);

/*
 * Phase 1.9 anti-rollback helpers — pure functions, host-testable.
 *
 * The floor is the highest image_version the bootloader has ever booted.
 * It is stored alongside the slot's monotonic_counter — see
 * docs/wiki/plans/001-bootloader/anti-rollback.md for the rationale and
 * the pristine-chip seeding behaviour.
 */

/* Maximum fail_count before the bootloader treats a slot as failed and
 * falls back. Three was picked to tolerate a couple of transient glitches
 * (brown-out mid-boot, ESD reset) without panicking. */
#define IMG_FAIL_COUNT_MAX  3u

/*
 * Returns 1 if `header.image_version >= floor`, 0 otherwise. The first
 * boot of a pristine chip has floor==0 and any signed image satisfies
 * that, so the chip seeds itself.
 */
int img_header_meets_floor(const img_header_t *header, uint32_t floor);

/*
 * Compute the rollback floor as the max of two slot's monotonic_counters.
 * `a_valid` / `b_valid` reflect whether the parser accepted that slot's
 * metadata — invalid sectors contribute 0. Either pointer may be NULL
 * when its corresponding _valid flag is 0.
 */
uint32_t img_compute_floor(int a_valid, const img_slot_metadata_t *a,
                           int b_valid, const img_slot_metadata_t *b);

/*
 * Returns the new floor value to commit after a successful boot of `slot`,
 * given the current floor and the verified header's image_version. The
 * result is always >= the current floor and >= header->image_version, so
 * the floor is monotonic non-decreasing across boots.
 */
uint32_t img_compute_new_floor(uint32_t current_floor, uint32_t image_version);

/*
 * Increment a fail_count value, clamping at IMG_FAIL_COUNT_MAX. The
 * bootloader calls this before jump; the app's
 * bl_handshake_clear_fail_count() resets to zero on a clean boot.
 */
uint32_t img_fail_count_increment(uint32_t current);

/*
 * Returns 1 if `fc >= IMG_FAIL_COUNT_MAX`, i.e. the slot has crashed
 * enough times that the bootloader should fall back to the other slot.
 */
int img_fail_count_tripped(uint32_t fc);

/*
 * Compute the trailing CRC for an img_slot_metadata_t in-place.  The
 * caller is expected to have already populated every field except the
 * trailing crc; magic / metadata_version / reserved are also rewritten
 * to known values so a partially-zeroed-out struct passes
 * img_slot_metadata_parse() afterwards.  Used by both the OTA receiver
 * and the boot-time floor / fail_count writer.
 */
void img_slot_metadata_finalize(img_slot_metadata_t *md);

/*
 * Pick the "active" slot from a pair of parsed metadata blobs.  Returns
 * 0 for slot A, 1 for slot B (matching flash_slot_id_t).  Decision tree:
 *   - both valid + exactly one active flag → that slot
 *   - both valid + both/no active flags    → higher monotonic_counter
 *   - one valid, one invalid                → the valid one
 *   - both invalid                          → slot A (pristine-chip default)
 *
 * Pure function — host-testable, no I/O.  Used by both the bootloader's
 * boot-path slot pick and the OTA receiver's "which slot is currently
 * active?" query.
 */
int img_pick_active_slot(int a_valid, const img_slot_metadata_t *a,
                         int b_valid, const img_slot_metadata_t *b);

#ifdef __cplusplus
}
#endif

#endif /* LIB_IMG_HEADER_H */
