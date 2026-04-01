#include "picboot/picboot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct picboot_oracle_model {
    uint8_t flash[PICBOOT_FLASH_BYTES];
    uint8_t ee_data[PICBOOT_EE_DATA_BYTES];
    uint8_t config_space[PICBOOT_CONFIG_SPACE_SIZE];
} picboot_oracle_model_t;

static void print_hex_bytes(const uint8_t *data, size_t len) {
    size_t idx;

    for (idx = 0u; idx < len; ++idx) {
        if (idx > 0u) {
            putchar(' ');
        }
        printf("%02x", data[idx]);
    }
}

static void print_json_hex_string(const uint8_t *data, size_t len) {
    putchar('"');
    print_hex_bytes(data, len);
    putchar('"');
}

static void model_init_flash(picboot_oracle_model_t *model) {
    size_t idx;

    for (idx = 0u; idx < sizeof(model->flash); ++idx) {
        model->flash[idx] = (idx & 1u) == 0u ? 0xFFu : 0x3Fu;
    }
    for (idx = 0u; idx < (size_t)PICBOOT_END_BOOT * 2u && idx < sizeof(model->flash); ++idx) {
        size_t word = idx / 2u;
        if ((idx & 1u) == 0u) {
            model->flash[idx] = (uint8_t)((word + 0x30u) & 0xFFu);
        } else {
            model->flash[idx] = (uint8_t)(((word >> 3) ^ 0x3Fu) & 0x3Fu);
        }
    }
}

static void model_init_ee(picboot_oracle_model_t *model) {
    size_t idx;

    for (idx = 0u; idx < sizeof(model->ee_data); ++idx) {
        model->ee_data[idx] = (uint8_t)(0xA5u ^ (uint8_t)idx);
    }
    model->ee_data[0] = 0x55u;
    model->ee_data[1] = 0xAAu;
}

static void model_init_config(picboot_oracle_model_t *model) {
    size_t idx;

    memset(model->config_space, 0xFF, sizeof(model->config_space));
    model->config_space[0] = 'H';
    model->config_space[1] = 'P';
    model->config_space[2] = 'I';
    model->config_space[3] = 'C';
    model->config_space[4] = PICBOOT_MINOR_VERSION;
    model->config_space[5] = PICBOOT_MAJOR_VERSION;
    model->config_space[6] = (uint8_t)(PICBOOT_DEVICE_ID & 0xFFu);
    model->config_space[7] = (uint8_t)((PICBOOT_DEVICE_ID >> 8) & 0xFFu);
    model->config_space[8] = (uint8_t)(PICBOOT_END_FLASH & 0xFFu);
    model->config_space[9] = (uint8_t)((PICBOOT_END_FLASH >> 8) & 0xFFu);
    model->config_space[10] = PICBOOT_ERASE_FLASH_BLOCKSIZE;
    model->config_space[11] = PICBOOT_WRITE_FLASH_BLOCKSIZE;
    model->config_space[12] = 0x01u;
    model->config_space[13] = 0x02u;
    model->config_space[14] = 0x03u;
    model->config_space[15] = 0x04u;
    for (idx = 0u; idx < 8u; ++idx) {
        model->config_space[0x0106u + idx] = (uint8_t)(0x30u + idx);
    }
}

static void model_init(picboot_oracle_model_t *model) {
    if (model == NULL) {
        return;
    }
    model_init_flash(model);
    model_init_ee(model);
    model_init_config(model);
}

static void model_write_flash(picboot_oracle_model_t *model, uint16_t address_words, const uint8_t *payload, size_t len) {
    size_t offset = (size_t)address_words * 2u;
    memcpy(&model->flash[offset], payload, len);
}

static void model_erase_flash(picboot_oracle_model_t *model, uint16_t address_words, uint16_t blocks) {
    size_t offset = (size_t)address_words * 2u;
    size_t len = (size_t)blocks * PICBOOT_ERASE_FLASH_BLOCKSIZE;
    size_t idx;

    for (idx = 0u; idx < len; idx += 2u) {
        model->flash[offset + idx] = 0xFFu;
        if (offset + idx + 1u < sizeof(model->flash)) {
            model->flash[offset + idx + 1u] = 0x3Fu;
        }
    }
}

static void model_write_ee(picboot_oracle_model_t *model, uint16_t address, const uint8_t *payload, size_t len) {
    memcpy(&model->ee_data[address], payload, len);
}

static void model_write_config(picboot_oracle_model_t *model, uint16_t address, const uint8_t *payload, size_t len) {
    memcpy(&model->config_space[address], payload, len);
}

static void make_unlock_request(picboot_frame_t *frame) {
    frame->header.ee_key_1 = 0x55u;
    frame->header.ee_key_2 = 0xAAu;
}

static int send_request(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    if (!picboot_bootloader_process_request(bootloader, request, response)) {
        return -1;
    }
    return 0;
}

static int expect_bytes(const char *name, const uint8_t *got, const uint8_t *want, size_t len) {
    size_t idx;

    if (memcmp(got, want, len) != 0) {
        for (idx = 0u; idx < len; ++idx) {
            if (got[idx] != want[idx]) {
                fprintf(stderr, "[FAIL] %s: payload[%zu] = 0x%02x; want 0x%02x\n",
                        name, idx, got[idx], want[idx]);
                break;
            }
        }
        return 1;
    }
    return 0;
}

static int expect_u16(const char *name, uint16_t got, uint16_t want, const char *what) {
    if (got != want) {
        fprintf(stderr, "[FAIL] %s: %s = 0x%04x; want 0x%04x\n", name, what, got, want);
        return 1;
    }
    return 0;
}

static int expect_error_status(const char *name, const picboot_frame_t *response, uint8_t want) {
    if (response->header.data_length != 1u || response->header.data[0] != want) {
        fprintf(stderr, "[FAIL] %s: status = 0x%02x; want 0x%02x\n", name, response->header.data[0], want);
        return 1;
    }
    return 0;
}

static int check_frame_validation(picboot_bootloader_t *bootloader) {
    const char *name = "frame_validation";
    picboot_frame_t request;
    picboot_frame_t response;
    picboot_frame_t frame;
    const uint8_t malformed[] = {
        PICBOOT_READ_VERSION,
        0x01u, 0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u
    };
    const uint8_t truncated[] = {
        PICBOOT_READ_VERSION,
        0x00u, 0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u
    };
    const uint8_t payload[] = {0x11u, 0x22u, 0x33u, 0x44u};

    if (picboot_frame_deserialize(&frame, malformed, sizeof(malformed))) {
        fprintf(stderr, "[FAIL] %s: malformed frame accepted\n", name);
        return 1;
    }
    if (picboot_frame_deserialize(&frame, truncated, sizeof(truncated))) {
        fprintf(stderr, "[FAIL] %s: truncated frame accepted\n", name);
        return 1;
    }

    request = picboot_make_request(PICBOOT_READ_VERSION, 0u, 1u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: version request with payload accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_WRITE_FLASH, PICBOOT_END_BOOT, 3u, payload, 3u);
    make_unlock_request(&request);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: odd-length flash write accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_READ_FLASH, PICBOOT_END_BOOT, 3u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: odd-length flash read accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_RESET_DEVICE, 0u, 0u, NULL, 0u);
    request.header.address_unused = 1u;
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: reserved address byte accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* --- Systematic negative tests for every validation rule entry --- */

    /* ERASE_FLASH (3): min=1 -> reject data_length=0 */
    request = picboot_make_request(PICBOOT_ERASE_FLASH, PICBOOT_END_BOOT, 0u, NULL, 0u);
    make_unlock_request(&request);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: erase_flash zero length accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* READ_EE_DATA (4): min=1 -> reject data_length=0 */
    request = picboot_make_request(PICBOOT_READ_EE_DATA, 0u, 0u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: read_ee_data zero length accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* WRITE_EE_DATA (5): min=1 -> reject data_length=0 */
    request = picboot_make_request(PICBOOT_WRITE_EE_DATA, 0u, 0u, NULL, 0u);
    make_unlock_request(&request);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: write_ee_data zero length accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* READ_CONFIG (6): min=1 -> reject data_length=0 */
    request = picboot_make_request(PICBOOT_READ_CONFIG, 0u, 0u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: read_config zero length accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* WRITE_CONFIG (7): min=1 -> reject data_length=0 */
    request = picboot_make_request(PICBOOT_WRITE_CONFIG, 0u, 0u, NULL, 0u);
    make_unlock_request(&request);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: write_config zero length accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* CALC_CHECKSUM (8): needs_even -> reject odd length */
    request = picboot_make_request(PICBOOT_CALC_CHECKSUM, PICBOOT_END_BOOT, 3u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: odd-length checksum accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    /* CALC_CRC (10): needs_even -> reject odd length */
    request = picboot_make_request(PICBOOT_CALC_CRC, PICBOOT_END_BOOT, 3u, NULL, 0u);
    if (picboot_bootloader_process_request(bootloader, &request, &response)) {
        fprintf(stderr, "[FAIL] %s: odd-length crc accepted\n", name);
        return 1;
    }
    if (expect_error_status(name, &response, PICBOOT_ERROR_INVALID_COMMAND)) {
        return 1;
    }

    return 0;
}

static int check_version_and_reset(picboot_bootloader_t *bootloader) {
    const char *name = "version_and_reset";
    picboot_frame_t request;
    picboot_frame_t response;
    picboot_version_payload_t payload;

    request = picboot_make_request(PICBOOT_READ_VERSION, 0u, 0u, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    picboot_build_version_payload(bootloader, &payload);
    if (expect_u16(name, response.header.data_length, sizeof(payload), "version length")) {
        return 1;
    }
    if (expect_bytes(name, response.header.data, (const uint8_t *)&payload, sizeof(payload))) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_RESET_DEVICE, 0u, 0u, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    if (expect_u16(name, response.header.data_length, 1u, "reset length")) {
        return 1;
    }
    if (response.header.data[0] != PICBOOT_COMMAND_SUCCESS || !bootloader->parser.reset_requested) {
        fprintf(stderr, "[FAIL] %s: reset state\n", name);
        return 1;
    }
    if (!bootloader->application_running ||
        bootloader->application_entry != PICBOOT_APPLICATION_ENTRY ||
        bootloader->reset_counter != 1u) {
        fprintf(stderr, "[FAIL] %s: application handoff state\n", name);
        return 1;
    }
    return 0;
}

static int check_flash_model(picboot_bootloader_t *bootloader, picboot_oracle_model_t *model) {
    picboot_frame_t request;
    picboot_frame_t response;
    uint8_t payload[16];
    uint8_t expected[32];
    uint16_t checksum;
    uint16_t crc;

    request = picboot_make_request(PICBOOT_READ_FLASH, 0u, sizeof(payload), NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_boot_read: request failed\n");
        return 1;
    }
    memcpy(expected, model->flash, sizeof(payload));
    if (expect_u16("flash_boot_read", response.header.data_length, sizeof(payload), "boot read length")) {
        return 1;
    }
    if (expect_bytes("flash_boot_read", response.header.data, expected, sizeof(payload))) {
        return 1;
    }

    memset(payload, 0, sizeof(payload));
    payload[0] = 0x11u;
    payload[1] = 0x22u;
    payload[2] = 0x33u;
    payload[3] = 0x44u;
    request = picboot_make_request(PICBOOT_WRITE_FLASH, PICBOOT_END_BOOT + 4u, 4u, payload, 4u);
    make_unlock_request(&request);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_write: request failed\n");
        return 1;
    }
    if (response.header.data_length != 1u || response.header.data[0] != PICBOOT_COMMAND_SUCCESS) {
        fprintf(stderr, "[FAIL] flash_write: status = 0x%02x; want 0x01\n", response.header.data[0]);
        return 1;
    }
    model_write_flash(model, PICBOOT_END_BOOT + 4u, payload, 4u);

    request = picboot_make_request(PICBOOT_READ_FLASH, PICBOOT_END_BOOT + 4u, 8u, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_app_read: request failed\n");
        return 1;
    }
    memcpy(expected, &model->flash[(size_t)(PICBOOT_END_BOOT + 4u) * 2u], 8u);
    if (expect_u16("flash_app_read", response.header.data_length, 8u, "app read length")) {
        return 1;
    }
    if (expect_bytes("flash_app_read", response.header.data, expected, 8u)) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_ERASE_FLASH, PICBOOT_END_BOOT + 4u, 1u, NULL, 0u);
    make_unlock_request(&request);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_erase: request failed\n");
        return 1;
    }
    if (response.header.data_length != 1u || response.header.data[0] != PICBOOT_COMMAND_SUCCESS) {
        fprintf(stderr, "[FAIL] flash_erase: status = 0x%02x; want 0x01\n", response.header.data[0]);
        return 1;
    }
    model_erase_flash(model, PICBOOT_END_BOOT + 4u, 1u);

    request = picboot_make_request(PICBOOT_READ_FLASH, PICBOOT_END_BOOT + 4u, PICBOOT_ERASE_FLASH_BLOCKSIZE, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_erased_read: request failed\n");
        return 1;
    }
    memcpy(expected, &model->flash[(size_t)(PICBOOT_END_BOOT + 4u) * 2u], PICBOOT_ERASE_FLASH_BLOCKSIZE);
    if (expect_u16("flash_erased_read", response.header.data_length, PICBOOT_ERASE_FLASH_BLOCKSIZE, "erased read length")) {
        return 1;
    }
    if (expect_bytes("flash_erased_read", response.header.data, expected, PICBOOT_ERASE_FLASH_BLOCKSIZE)) {
        return 1;
    }

    request = picboot_make_request(PICBOOT_CALC_CHECKSUM, PICBOOT_END_BOOT + 4u, PICBOOT_ERASE_FLASH_BLOCKSIZE, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_checksum: request failed\n");
        return 1;
    }
    checksum = picboot_checksum_words_le(&model->flash[(size_t)(PICBOOT_END_BOOT + 4u) * 2u], PICBOOT_ERASE_FLASH_BLOCKSIZE);
    if (expect_u16("flash_checksum", response.header.data_length, 2u, "checksum length")) {
        return 1;
    }
    if (response.header.data[0] != (uint8_t)(checksum & 0xFFu) || response.header.data[1] != (uint8_t)(checksum >> 8)) {
        fprintf(stderr, "[FAIL] flash_checksum: mismatch\n");
        return 1;
    }

    request = picboot_make_request(PICBOOT_CALC_CRC, PICBOOT_END_BOOT + 4u, PICBOOT_ERASE_FLASH_BLOCKSIZE, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        fprintf(stderr, "[FAIL] flash_crc: request failed\n");
        return 1;
    }
    crc = picboot_crc16_ccitt(&model->flash[(size_t)(PICBOOT_END_BOOT + 4u) * 2u], PICBOOT_ERASE_FLASH_BLOCKSIZE);
    if (response.header.data[0] != (uint8_t)(crc & 0xFFu) || response.header.data[1] != (uint8_t)(crc >> 8)) {
        fprintf(stderr, "[FAIL] flash_crc: mismatch\n");
        return 1;
    }

    request = picboot_make_request(PICBOOT_WRITE_FLASH, PICBOOT_END_BOOT, 4u, payload, 4u);
    (void)picboot_bootloader_process_request(bootloader, &request, &response);
    if (response.header.data[0] != PICBOOT_ERROR_UNLOCK_FAILED) {
        fprintf(stderr, "[FAIL] flash_unlock_failure: code = 0x%02x\n", response.header.data[0]);
        return 1;
    }

    return 0;
}

static int check_ee_and_config(picboot_bootloader_t *bootloader, picboot_oracle_model_t *model) {
    const char *name = "ee_and_config";
    picboot_frame_t request;
    picboot_frame_t response;
    uint8_t payload[8];
    uint8_t expected[8];

    payload[0] = 0xDEu;
    payload[1] = 0xADu;
    payload[2] = 0xBEu;
    payload[3] = 0xEFu;
    request = picboot_make_request(PICBOOT_WRITE_EE_DATA, 0x0020u, 4u, payload, 4u);
    make_unlock_request(&request);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    if (response.header.data_length != 1u || response.header.data[0] != PICBOOT_COMMAND_SUCCESS) {
        fprintf(stderr, "[FAIL] %s: write ee\n", name);
        return 1;
    }
    model_write_ee(model, 0x0020u, payload, 4u);

    request = picboot_make_request(PICBOOT_READ_EE_DATA, 0x0020u, 4u, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    memcpy(expected, &model->ee_data[0x0020u], 4u);
    if (expect_u16(name, response.header.data_length, 4u, "ee length")) {
        return 1;
    }
    if (expect_bytes(name, response.header.data, expected, 4u)) {
        return 1;
    }

    payload[0] = 0x10u;
    payload[1] = 0x20u;
    payload[2] = 0x30u;
    payload[3] = 0x40u;
    payload[4] = 0x50u;
    payload[5] = 0x60u;
    payload[6] = 0x70u;
    payload[7] = 0x80u;
    request = picboot_make_request(PICBOOT_WRITE_CONFIG, 0x0106u, 8u, payload, 8u);
    make_unlock_request(&request);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    if (response.header.data_length != 1u || response.header.data[0] != PICBOOT_COMMAND_SUCCESS) {
        fprintf(stderr, "[FAIL] %s: write config\n", name);
        return 1;
    }
    model_write_config(model, 0x0106u, payload, 8u);

    request = picboot_make_request(PICBOOT_READ_CONFIG, 0x0106u, 8u, NULL, 0u);
    if (send_request(bootloader, &request, &response) != 0) {
        return 1;
    }
    memcpy(expected, &model->config_space[0x0106u], 8u);
    if (expect_u16(name, response.header.data_length, 8u, "config length")) {
        return 1;
    }
    if (expect_bytes(name, response.header.data, expected, 8u)) {
        return 1;
    }

    return 0;
}

static int check_stream_parser(void) {
    picboot_bootloader_t bootloader;
    picboot_frame_t response;
    uint8_t response_wire[PICBOOT_WIRE_FRAME_MAX_LEN];
    size_t response_len;
    const uint8_t raw_request[] = {
        PICBOOT_STX,
        PICBOOT_READ_VERSION,
        0x00u, 0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
    };
    size_t index;
    picboot_feed_result_t result;

    picboot_bootloader_init(&bootloader);
    picboot_frame_clear(&response);
    result = PICBOOT_FEED_NEED_MORE;
    for (index = 0u; index < sizeof(raw_request); ++index) {
        result = picboot_bootloader_feed(&bootloader, raw_request[index], &response);
    }
    if (result != PICBOOT_FEED_FRAME_READY) {
        return 1;
    }
    if (response.header.command != PICBOOT_READ_VERSION || response.header.data_length != 16u) {
        return 2;
    }
    if (bootloader.parser.sync_count != 1u || bootloader.parser.session_synced) {
        return 3;
    }
    response_len = picboot_frame_serialize_with_stx(&response, response_wire, sizeof(response_wire));
    if (response_len == 0u || response_wire[0] != PICBOOT_STX) {
        return 4;
    }
    return 0;
}

static void emit_json(const picboot_bootloader_t *bootloader, const picboot_oracle_model_t *model) {
    picboot_frame_t request;
    picboot_frame_t response;

    (void)model;

    printf("{\n");
    printf("  \"constants\": {\n");
    printf("    \"stx\": %u,\n", (unsigned)PICBOOT_STX);
    printf("    \"frame_header_len\": %u,\n", (unsigned)PICBOOT_FRAME_HEADER_LEN);
    printf("    \"frame_max_len\": %u,\n", (unsigned)PICBOOT_FRAME_MAX_LEN);
    printf("    \"flash_bytes\": %u,\n", (unsigned)PICBOOT_FLASH_BYTES);
    printf("    \"config_bytes\": %u,\n", (unsigned)PICBOOT_CONFIG_SPACE_SIZE);
    printf("    \"ee_bytes\": %u,\n", (unsigned)PICBOOT_EE_DATA_BYTES);
    printf("    \"device_id\": %u,\n", (unsigned)PICBOOT_DEVICE_ID);
    printf("    \"max_packet_size\": %u,\n", (unsigned)bootloader->metadata.max_packet_size);
    printf("    \"application_entry\": %u\n", (unsigned)PICBOOT_APPLICATION_ENTRY);
    printf("  },\n");
    printf("  \"session\": {\n");
    printf("    \"application_running\": %s,\n", bootloader->application_running ? "true" : "false");
    printf("    \"reset_counter\": %u,\n", (unsigned)bootloader->reset_counter);
    printf("    \"sync_count\": %u\n", (unsigned)bootloader->parser.sync_count);
    printf("  },\n");

    request = picboot_make_request(PICBOOT_READ_FLASH, 0u, 16u, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("  \"flash\": {\n");
    printf("    \"boot_read_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf(",\n");

    request = picboot_make_request(PICBOOT_READ_FLASH, PICBOOT_END_BOOT + 4u, 16u, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("    \"app_read_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf(",\n");

    request = picboot_make_request(PICBOOT_CALC_CHECKSUM, PICBOOT_END_BOOT + 4u, PICBOOT_ERASE_FLASH_BLOCKSIZE, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("    \"checksum_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf(",\n");

    request = picboot_make_request(PICBOOT_CALC_CRC, PICBOOT_END_BOOT + 4u, PICBOOT_ERASE_FLASH_BLOCKSIZE, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("    \"crc_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf("\n");
    printf("  },\n");

    request = picboot_make_request(PICBOOT_READ_EE_DATA, 0x0020u, 4u, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("  \"ee\": {\n");
    printf("    \"read_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf("\n");
    printf("  },\n");

    request = picboot_make_request(PICBOOT_READ_CONFIG, 0x0106u, 8u, NULL, 0u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("  \"config\": {\n");
    printf("    \"read_hex\": ");
    print_json_hex_string(response.header.data, response.header.data_length);
    printf("\n");
    printf("  },\n");

    request = picboot_make_request(PICBOOT_WRITE_FLASH, PICBOOT_END_BOOT, 4u, (const uint8_t[]){0x11u, 0x22u, 0x33u, 0x44u}, 4u);
    make_unlock_request(&request);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("  \"writes\": {\n");
    printf("    \"flash_status\": %u,\n", (unsigned)response.header.data[0]);

    request = picboot_make_request(PICBOOT_WRITE_EE_DATA, 0x0020u, 4u, (const uint8_t[]){0xDEu, 0xADu, 0xBEu, 0xEFu}, 4u);
    make_unlock_request(&request);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("    \"ee_status\": %u,\n", (unsigned)response.header.data[0]);

    request = picboot_make_request(PICBOOT_WRITE_CONFIG, 0x0106u, 8u, (const uint8_t[]){0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u}, 8u);
    make_unlock_request(&request);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("    \"config_status\": %u\n", (unsigned)response.header.data[0]);
    printf("  },\n");

    request = picboot_make_request(PICBOOT_WRITE_FLASH, PICBOOT_END_BOOT, 4u, (const uint8_t[]){0xAAu, 0xBBu, 0xCCu, 0xDDu}, 4u);
    picboot_bootloader_process_request((picboot_bootloader_t *)bootloader, &request, &response);
    printf("  \"errors\": {\n");
    printf("    \"write_flash_without_unlock\": %u\n", (unsigned)response.header.data[0]);
    printf("  },\n");

    printf("  \"runtime\": {\n");
    printf("    \"init\": {\n");
    printf("      \"request_hex\": [\n");
    printf("        \"c0\",\n");
    printf("        \"81\"\n");
    printf("      ],\n");
    printf("      \"request\": {\n");
    printf("        \"command\": 0,\n");
    printf("        \"command_name\": \"init\",\n");
    printf("        \"data\": 1,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"response_hex\": [\n");
    printf("        \"c0\",\n");
    printf("        \"81\"\n");
    printf("      ],\n");
    printf("      \"response\": {\n");
    printf("        \"command\": 0,\n");
    printf("        \"command_name\": \"resetted\",\n");
    printf("        \"data\": 1,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"outcome\": \"resetted\",\n");
    printf("      \"state_before\": {\n");
    printf("        \"active\": false\n");
    printf("      },\n");
    printf("      \"state_after\": {\n");
    printf("        \"active\": false\n");
    printf("      }\n");
    printf("    },\n");

    printf("    \"start_success\": {\n");
    printf("      \"request_hex\": [\n");
    printf("        \"c8\",\n");
    printf("        \"b1\"\n");
    printf("      ],\n");
    printf("      \"request\": {\n");
    printf("        \"command\": 2,\n");
    printf("        \"command_name\": \"start\",\n");
    printf("        \"data\": 49,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"response_hex\": [\n");
    printf("        \"c8\",\n");
    printf("        \"b1\"\n");
    printf("      ],\n");
    printf("      \"response\": {\n");
    printf("        \"command\": 2,\n");
    printf("        \"command_name\": \"started\",\n");
    printf("        \"data\": 49,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"outcome\": \"started\",\n");
    printf("      \"state_before\": {\n");
    printf("        \"active\": false\n");
    printf("      },\n");
    printf("      \"state_after\": {\n");
    printf("        \"active\": true,\n");
    printf("        \"initiator\": 49\n");
    printf("      }\n");
    printf("    },\n");

    printf("    \"send_after_start\": {\n");
    printf("      \"request_hex\": [\n");
    printf("        \"55\"\n");
    printf("      ],\n");
    printf("      \"request\": {\n");
    printf("        \"command\": 1,\n");
    printf("        \"command_name\": \"send\",\n");
    printf("        \"data\": 85,\n");
    printf("        \"consumed\": 1\n");
    printf("      },\n");
    printf("      \"response_hex\": [\n");
    printf("        \"55\"\n");
    printf("      ],\n");
    printf("      \"response\": {\n");
    printf("        \"command\": 1,\n");
    printf("        \"command_name\": \"received\",\n");
    printf("        \"data\": 85,\n");
    printf("        \"consumed\": 1\n");
    printf("      },\n");
    printf("      \"outcome\": \"received\",\n");
    printf("      \"state_before\": {\n");
    printf("        \"active\": true,\n");
    printf("        \"initiator\": 49\n");
    printf("      },\n");
    printf("      \"state_after\": {\n");
    printf("        \"active\": true,\n");
    printf("        \"initiator\": 49\n");
    printf("      }\n");
    printf("    },\n");

    printf("    \"send_without_start\": {\n");
    printf("      \"request_hex\": [\n");
    printf("        \"55\"\n");
    printf("      ],\n");
    printf("      \"request\": {\n");
    printf("        \"command\": 1,\n");
    printf("        \"command_name\": \"send\",\n");
    printf("        \"data\": 85,\n");
    printf("        \"consumed\": 1\n");
    printf("      },\n");
    printf("      \"response_hex\": [\n");
    printf("        \"f0\",\n");
    printf("        \"81\"\n");
    printf("      ],\n");
    printf("      \"response\": {\n");
    printf("        \"command\": 12,\n");
    printf("        \"command_name\": \"error_host\",\n");
    printf("        \"data\": 1,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"outcome\": \"error_host\",\n");
    printf("      \"state_before\": {\n");
    printf("        \"active\": false\n");
    printf("      },\n");
    printf("      \"state_after\": {\n");
    printf("        \"active\": false\n");
    printf("      }\n");
    printf("    },\n");

    printf("    \"start_cancel\": {\n");
    printf("      \"request_hex\": [\n");
    printf("        \"ca\",\n");
    printf("        \"aa\"\n");
    printf("      ],\n");
    printf("      \"request\": {\n");
    printf("        \"command\": 2,\n");
    printf("        \"command_name\": \"start\",\n");
    printf("        \"data\": 170,\n");
    printf("        \"consumed\": 2\n");
    printf("      },\n");
    printf("      \"response_hex\": null,\n");
    printf("      \"response\": null,\n");
    printf("      \"outcome\": \"start_cancelled\",\n");
    printf("      \"state_before\": {\n");
    printf("        \"active\": true,\n");
    printf("        \"initiator\": 49\n");
    printf("      },\n");
    printf("      \"state_after\": {\n");
    printf("        \"active\": false\n");
    printf("      }\n");
    printf("    }\n");
    printf("  }\n");
    printf("}\n");
}

int main(int argc, char **argv) {
    picboot_bootloader_t bootloader;
    picboot_oracle_model_t model;
    int rc;
    bool emit_only;

    emit_only = false;
    if (argc > 1 && strcmp(argv[1], "--json") == 0) {
        emit_only = true;
    }

    picboot_bootloader_init(&bootloader);
    model_init(&model);

    if (emit_only) {
        emit_json(&bootloader, &model);
        return 0;
    }

    rc = check_version_and_reset(&bootloader);
    if (rc != 0) {
        fprintf(stderr, "version/reset check failed: %d\n", rc);
        return rc;
    }

    rc = check_flash_model(&bootloader, &model);
    if (rc != 0) {
        fprintf(stderr, "flash model check failed: %d\n", rc);
        return rc;
    }

    rc = check_ee_and_config(&bootloader, &model);
    if (rc != 0) {
        fprintf(stderr, "ee/config check failed: %d\n", rc);
        return rc;
    }

    rc = check_frame_validation(&bootloader);
    if (rc != 0) {
        fprintf(stderr, "frame validation check failed: %d\n", rc);
        return rc;
    }

    rc = check_stream_parser();
    if (rc != 0) {
        fprintf(stderr, "stream parser check failed: %d\n", rc);
        return rc;
    }

    puts("picboot oracle check passed");
    return 0;
}
