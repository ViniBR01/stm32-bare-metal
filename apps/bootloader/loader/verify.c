#include "verify.h"

#include "stm32f4xx.h"

#include "crypto.h"
#include "img_header.h"
#include "uart.h"

extern const uint8_t bootloader_pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN];

static const char *slot_name(flash_slot_id_t s)
{
    return (s == FLASH_SLOT_A) ? "A" : "B";
}

verify_status_t verify_slot(flash_slot_id_t slot, uint32_t *app_base_out,
                            uint32_t *cycles_out)
{
    const uint32_t slot_base = (slot == FLASH_SLOT_A) ? FLASH_SLOT_A_BASE
                                                       : FLASH_SLOT_B_BASE;

    img_header_t hdr;
    img_err_t rc = img_header_parse((const uint8_t *)slot_base,
                                    sizeof(img_header_t), &hdr);
    if (rc != IMG_OK) {
        uart_puts("BL: slot ");
        uart_puts(slot_name(slot));
        uart_puts(" header parse failed: rc=");
        uart_print_hex32((uint32_t)rc);
        uart_puts("\r\n");
        return VERIFY_FAIL_PARSE;
    }

    if (hdr.image_type != IMG_TYPE_APP) {
        uart_puts("BL: slot ");
        uart_puts(slot_name(slot));
        uart_puts(" image_type != APP\r\n");
        return VERIFY_FAIL_TYPE;
    }

    const uint8_t *payload = (const uint8_t *)(slot_base + hdr.payload_offset);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint8_t computed[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(payload, hdr.payload_size, computed);

    if (crypto_memcmp_ct(computed, hdr.sha256, CRYPTO_SHA256_DIGEST_LEN) != 0) {
        uart_puts("BL: slot ");
        uart_puts(slot_name(slot));
        uart_puts(" verify FAILED: sha mismatch\r\n");
        return VERIFY_FAIL_SHA;
    }

    if (crypto_ecdsa_p256_verify(bootloader_pubkey, computed, hdr.signature) != 1) {
        uart_puts("BL: slot ");
        uart_puts(slot_name(slot));
        uart_puts(" verify FAILED: ecdsa reject\r\n");
        return VERIFY_FAIL_ECDSA;
    }

    *cycles_out = DWT->CYCCNT;
    *app_base_out = slot_base + hdr.payload_offset;
    return VERIFY_OK;
}
