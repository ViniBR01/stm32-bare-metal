#include "img_header.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 * Bytewise CRC-32, no precomputed table. Per-byte cost is 8 shift-and-XOR
 * iterations: trivial for the host signer and acceptable for the bootloader,
 * which only ever CRCs a 140-byte header and a 36-byte metadata blob. Keeping
 * the implementation table-free saves ~1 KB of flash that the bootloader
 * sector cannot afford.
 */
uint32_t img_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (buf == NULL) {
        return crc ^ 0xFFFFFFFFu;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)buf[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static int image_type_is_known(uint32_t t)
{
    return (t == IMG_TYPE_BOOTLOADER) || (t == IMG_TYPE_APP);
}

img_err_t img_header_parse(const uint8_t *buf, size_t buf_len, img_header_t *out)
{
    if (buf == NULL || out == NULL) {
        return IMG_ERR_NULL_ARG;
    }
    if (buf_len < sizeof(img_header_t)) {
        return IMG_ERR_BAD_SIZE;
    }

    /*
     * The CRC covers every byte of the header up to (but excluding) the
     * trailing header_crc field. We compute it directly on the raw buffer so
     * struct member ordering is the single source of truth.
     */
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t expected_crc;
    memcpy(&expected_crc, buf + crc_offset, sizeof(expected_crc));

    uint32_t actual_crc = img_crc32(buf, crc_offset);
    if (actual_crc != expected_crc) {
        return IMG_ERR_BAD_CRC;
    }

    img_header_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));

    if (hdr.magic != IMG_HEADER_MAGIC) {
        return IMG_ERR_BAD_MAGIC;
    }
    if (hdr.header_version != IMG_HEADER_VERSION) {
        return IMG_ERR_BAD_VERSION;
    }
    if (!image_type_is_known(hdr.image_type)) {
        return IMG_ERR_BAD_TYPE;
    }
    if (hdr.payload_offset < sizeof(img_header_t)) {
        return IMG_ERR_BAD_OFFSET;
    }
    if (hdr.payload_size == 0) {
        return IMG_ERR_BAD_SIZE;
    }

    *out = hdr;
    return IMG_OK;
}

img_err_t img_slot_metadata_parse(const uint8_t *buf, size_t buf_len,
                                  img_slot_metadata_t *out)
{
    if (buf == NULL || out == NULL) {
        return IMG_ERR_NULL_ARG;
    }
    if (buf_len < sizeof(img_slot_metadata_t)) {
        return IMG_ERR_BAD_SIZE;
    }

    const size_t crc_offset = sizeof(img_slot_metadata_t) - sizeof(uint32_t);
    uint32_t expected_crc;
    memcpy(&expected_crc, buf + crc_offset, sizeof(expected_crc));

    uint32_t actual_crc = img_crc32(buf, crc_offset);
    if (actual_crc != expected_crc) {
        return IMG_ERR_BAD_CRC;
    }

    img_slot_metadata_t md;
    memcpy(&md, buf, sizeof(md));

    if (md.magic != IMG_SLOT_METADATA_MAGIC) {
        return IMG_ERR_BAD_MAGIC;
    }
    if (md.metadata_version != IMG_SLOT_METADATA_VERSION) {
        return IMG_ERR_BAD_VERSION;
    }

    *out = md;
    return IMG_OK;
}
