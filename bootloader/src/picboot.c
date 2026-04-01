#include "picboot/picboot.h"

#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(picboot_frame_header_t) == PICBOOT_FRAME_MAX_LEN, "picboot frame header layout must stay fixed");
/* Verify no padding — layout must match wire format */
_Static_assert(sizeof(picboot_version_payload_t) == 16u, "version payload size");
/* Offsets verified by construction: all fields are naturally aligned
 * (uint8_t at any offset, uint16_t at even offsets). The struct layout
 * 1+1+2+2+2+2+1+1+4 = 16 is guaranteed padding-free on all targets. */
#endif

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t picboot_request_address(const picboot_frame_t *request) {
    return read_u16_le(&request->header.address_l);
}

static uint16_t picboot_request_u16(const picboot_frame_t *request) {
    return read_u16_le((const uint8_t *)&request->header.data_length);
}

static bool picboot_request_has_unlock(const picboot_frame_t *request) {
    return request != NULL &&
           request->header.ee_key_1 == 0x55u &&
           request->header.ee_key_2 == 0xAAu;
}

static bool command_uses_request_payload(uint8_t command) {
    return command == PICBOOT_WRITE_FLASH ||
           command == PICBOOT_WRITE_EE_DATA ||
           command == PICBOOT_WRITE_CONFIG;
}

static bool picboot_request_is_structurally_valid(const picboot_frame_t *request, uint8_t *error_status) {
    uint16_t data_length;

    if (request == NULL) {
        if (error_status != NULL) {
            *error_status = PICBOOT_ERROR_INVALID_COMMAND;
        }
        return false;
    }

    data_length = read_u16_le((const uint8_t *)&request->header.data_length);
    if (request->header.address_u != 0u || request->header.address_unused != 0u) {
        if (error_status != NULL) {
            *error_status = PICBOOT_ERROR_INVALID_COMMAND;
        }
        return false;
    }

    switch (request->header.command) {
    case PICBOOT_READ_VERSION:
    case PICBOOT_RESET_DEVICE:
        if (data_length != 0u) {
            if (error_status != NULL) {
                *error_status = PICBOOT_ERROR_INVALID_COMMAND;
            }
            return false;
        }
        return true;

    case PICBOOT_READ_FLASH:
    case PICBOOT_CALC_CHECKSUM:
    case PICBOOT_CALC_CRC:
        if (data_length == 0u ||
            (data_length & 1u) != 0u ||
            data_length > sizeof(request->header.data)) {
            if (error_status != NULL) {
                *error_status = PICBOOT_ERROR_INVALID_COMMAND;
            }
            return false;
        }
        return true;

    case PICBOOT_ERASE_FLASH:
        if (data_length == 0u || data_length > (PICBOOT_FLASH_BYTES / PICBOOT_ERASE_FLASH_BLOCKSIZE)) {
            if (error_status != NULL) {
                *error_status = PICBOOT_ERROR_INVALID_COMMAND;
            }
            return false;
        }
        return true;

    case PICBOOT_READ_EE_DATA:
    case PICBOOT_WRITE_EE_DATA:
    case PICBOOT_READ_CONFIG:
    case PICBOOT_WRITE_CONFIG:
        if (data_length == 0u || data_length > sizeof(request->header.data)) {
            if (error_status != NULL) {
                *error_status = PICBOOT_ERROR_INVALID_COMMAND;
            }
            return false;
        }
        return true;

    case PICBOOT_WRITE_FLASH:
        if (data_length == 0u ||
            (data_length & 1u) != 0u ||
            data_length > sizeof(request->header.data)) {
            if (error_status != NULL) {
                *error_status = PICBOOT_ERROR_INVALID_COMMAND;
            }
            return false;
        }
        return true;
    }

    if (error_status != NULL) {
        *error_status = PICBOOT_ERROR_INVALID_COMMAND;
    }
    return false;
}

static void fill_word_erased(uint8_t *out, size_t len) {
    size_t idx;

    for (idx = 0u; idx < len && idx < PICBOOT_FLASH_BYTES; idx += 2u) {
        out[idx] = 0xFFu;
        if (idx + 1u < len) {
            out[idx + 1u] = 0x3Fu;
        }
    }
}

static void init_flash_model(picboot_bootloader_t *bootloader) {
    size_t idx;

    fill_word_erased(bootloader->flash, sizeof(bootloader->flash));
    for (idx = 0u; idx < PICBOOT_BOOT_REGION_BYTES; ++idx) {
        uint16_t word = (uint16_t)(idx / 2u);
        bootloader->flash[idx] = (idx & 1u) == 0u
            ? (uint8_t)((word + 0x30u) & 0xFFu)
            : (uint8_t)(((word >> 3) ^ 0x3Fu) & 0x3Fu);
    }
}

static void init_ee_model(picboot_bootloader_t *bootloader) {
    size_t idx;

    for (idx = 0u; idx < sizeof(bootloader->ee_data); ++idx) {
        bootloader->ee_data[idx] = (uint8_t)(0xA5u ^ (uint8_t)idx);
    }
    bootloader->ee_data[0] = 0x55u;
    bootloader->ee_data[1] = 0xAAu;
}

static void init_config_model(picboot_bootloader_t *bootloader) {
    size_t idx;

    memset(bootloader->config_space, 0xFF, sizeof(bootloader->config_space));
    bootloader->config_space[0] = bootloader->metadata.user_id[0];
    bootloader->config_space[1] = 0x1Fu;
    bootloader->config_space[2] = bootloader->metadata.user_id[1];
    bootloader->config_space[3] = 0x00u;
    bootloader->config_space[4] = bootloader->metadata.user_id[2];
    bootloader->config_space[5] = 0x00u;
    bootloader->config_space[6] = bootloader->metadata.user_id[3];
    bootloader->config_space[7] = 0x00u;
    write_u16_le(&bootloader->config_space[8], bootloader->metadata.device_id);
    write_u16_le(&bootloader->config_space[10], bootloader->metadata.flash_end);
    bootloader->config_space[12] = bootloader->metadata.erase_blocksize;
    bootloader->config_space[13] = bootloader->metadata.write_blocksize;
    bootloader->config_space[14] = bootloader->metadata.minor_version;
    bootloader->config_space[15] = bootloader->metadata.major_version;
    for (idx = 0u; idx < 8u; ++idx) {
        bootloader->config_space[0x0106u + idx] = (uint8_t)(0x30u + idx);
    }
}

static bool picboot_range_within(size_t start, size_t len, size_t limit) {
    return start <= limit && len <= (limit - start);
}

static bool picboot_flash_access_ok(uint16_t address_words, size_t length_bytes, bool writable) {
    size_t start;
    size_t len;

    start = (size_t)address_words * 2u;
    len = length_bytes;
    if (!picboot_range_within(start, len, PICBOOT_FLASH_BYTES)) {
        return false;
    }
    if (writable && start < (size_t)PICBOOT_END_BOOT * 2u) {
        return false;
    }
    return true;
}

static size_t picboot_flash_offset(uint16_t address_words) {
    return (size_t)address_words * 2u;
}

static uint16_t picboot_checksum_words_le_impl(const uint8_t *data, size_t len) {
    uint16_t check_sum = 0u;
    size_t idx;

    for (idx = 0u; idx < len && idx < PICBOOT_FLASH_BYTES; ++idx) {
        check_sum = (uint16_t)(check_sum + ((uint16_t)data[idx] << ((idx & 1u) == 0u ? 0u : 8u)));
    }
    return check_sum;
}

static uint16_t picboot_crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        if ((crc & 0x8000u) != 0u) {
            crc = (uint16_t)((crc << 1u) ^ 0x1021u);
        } else {
            crc <<= 1u;
        }
    }
    return crc;
}

static uint16_t picboot_crc16_ccitt_impl(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    size_t idx;

    for (idx = 0u; idx < len && idx < PICBOOT_FLASH_BYTES; ++idx) {
        crc = picboot_crc16_update(crc, data[idx]);
    }
    return crc;
}

static void set_error_response(const picboot_frame_t *request, picboot_frame_t *response, uint8_t status) {
    picboot_frame_clear(response);
    response->header.command = request->header.command;
    response->header.data_length = 1u;
    response->header.address_l = request->header.address_l;
    response->header.address_h = request->header.address_h;
    response->header.address_u = request->header.address_u;
    response->header.data[0] = status;
}

static void set_success_response(const picboot_frame_t *request, picboot_frame_t *response, const uint8_t *payload, size_t payload_len) {
    picboot_frame_clear(response);
    if (payload_len > sizeof(response->header.data)) {
        payload_len = sizeof(response->header.data);
    }
    response->header.command = request->header.command;
    response->header.data_length = (uint16_t)payload_len;
    response->header.address_l = request->header.address_l;
    response->header.address_h = request->header.address_h;
    response->header.address_u = request->header.address_u;
    response->header.address_unused = request->header.address_unused;
    if (payload_len > 0u) {
        memcpy(response->header.data, payload, payload_len);
    }
}

void picboot_frame_clear(picboot_frame_t *frame) {
    if (frame == NULL) {
        return;
    }
    memset(frame->raw, 0, sizeof(frame->raw));
}

void picboot_metadata_init(picboot_metadata_t *metadata) {
    if (metadata == NULL) {
        return;
    }
    metadata->minor_version = PICBOOT_MINOR_VERSION;
    metadata->major_version = PICBOOT_MAJOR_VERSION;
    metadata->max_packet_size = PICBOOT_FRAME_MAX_LEN;
    metadata->firmware_checksum = PICBOOT_BOOTLOADER_CHECKSUM;
    metadata->device_id = PICBOOT_DEVICE_ID;
    metadata->flash_end = PICBOOT_END_FLASH;
    metadata->erase_blocksize = PICBOOT_ERASE_FLASH_BLOCKSIZE;
    metadata->write_blocksize = PICBOOT_WRITE_FLASH_BLOCKSIZE;
    metadata->user_id[0] = 0x01u;
    metadata->user_id[1] = 0x02u;
    metadata->user_id[2] = 0x03u;
    metadata->user_id[3] = 0x04u;
}

void picboot_bootloader_init(picboot_bootloader_t *bootloader) {
    if (bootloader == NULL) {
        return;
    }
    picboot_metadata_init(&bootloader->metadata);
    picboot_bootloader_init_with_metadata(bootloader, &bootloader->metadata);
}

void picboot_bootloader_init_with_metadata(picboot_bootloader_t *bootloader, const picboot_metadata_t *metadata) {
    if (bootloader == NULL) {
        return;
    }
    if (metadata != NULL) {
        bootloader->metadata = *metadata;
    } else {
        picboot_metadata_init(&bootloader->metadata);
    }
    init_flash_model(bootloader);
    if (bootloader->metadata.firmware_checksum == PICBOOT_BOOTLOADER_CHECKSUM) {
        bootloader->metadata.firmware_checksum =
            picboot_checksum_words_le(bootloader->flash, PICBOOT_END_BOOT * 2u);
    }
    init_ee_model(bootloader);
    init_config_model(bootloader);
    bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
    bootloader->parser.header_bytes = 0u;
    bootloader->parser.payload_bytes = 0u;
    bootloader->parser.payload_expected = 0u;
    bootloader->parser.reset_requested = false;
    bootloader->parser.session_synced = false;
    bootloader->parser.sync_count = 0u;
    bootloader->parser.rx_frames = 0u;
    bootloader->parser.tx_frames = 0u;
    bootloader->parser.parse_errors = 0u;
    bootloader->parser.sync_loss_bytes = 0u;
    bootloader->application_running = false;
    bootloader->application_entry = PICBOOT_APPLICATION_ENTRY;
    bootloader->reset_counter = 0u;
    picboot_frame_clear(&bootloader->parser.current);
}

size_t picboot_frame_serialize(const picboot_frame_t *frame, uint8_t *out, size_t out_capacity) {
    size_t needed;

    if (frame == NULL || out == NULL) {
        return 0u;
    }

    needed = PICBOOT_FRAME_HEADER_LEN + (size_t)read_u16_le((const uint8_t *)&frame->header.data_length);
    if (needed > out_capacity || needed > PICBOOT_FRAME_MAX_LEN) {
        return 0u;
    }

    memcpy(out, frame->raw, needed);
    return needed;
}

size_t picboot_frame_serialize_with_stx(const picboot_frame_t *frame, uint8_t *out, size_t out_capacity) {
    size_t frame_len;

    if (frame == NULL || out == NULL || out_capacity < 1u) {
        return 0u;
    }

    frame_len = picboot_frame_serialize(frame, out + 1u, out_capacity - 1u);
    if (frame_len == 0u) {
        return 0u;
    }
    out[0] = PICBOOT_STX;
    return frame_len + 1u;
}

bool picboot_frame_deserialize(picboot_frame_t *frame, const uint8_t *data, size_t len) {
    uint16_t payload_len;
    size_t expected_len;

    if (frame == NULL || data == NULL || len < PICBOOT_FRAME_HEADER_LEN || len > PICBOOT_FRAME_MAX_LEN) {
        return false;
    }
    payload_len = read_u16_le(&data[1]);
    if (payload_len > sizeof(frame->header.data)) {
        return false;
    }
    expected_len = PICBOOT_FRAME_HEADER_LEN + (size_t)payload_len;
    if (len != expected_len) {
        return false;
    }
    picboot_frame_clear(frame);
    memcpy(frame->raw, data, len);
    return true;
}

bool picboot_deadline_reached_u32(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

bool picboot_deadline_reached_u16(uint16_t now, uint16_t deadline) {
    return (int16_t)(now - deadline) >= 0;
}

size_t picboot_expected_request_payload_len(uint8_t command, uint16_t request_data_length) {
    if (command_uses_request_payload(command)) {
        return request_data_length;
    }
    return 0u;
}

picboot_frame_t picboot_make_request(uint8_t command, uint16_t address, uint16_t data_length, const uint8_t *payload, size_t payload_len) {
    picboot_frame_t frame;
    picboot_frame_clear(&frame);
    frame.header.command = command;
    frame.header.data_length = data_length;
    write_u16_le(&frame.header.address_l, address);
    if (payload != NULL && payload_len > 0u) {
        memcpy(frame.header.data, payload, payload_len);
    }
    return frame;
}

picboot_frame_t picboot_make_response(uint8_t command, uint16_t address, const uint8_t *payload, size_t payload_len) {
    picboot_frame_t frame;
    picboot_frame_clear(&frame);
    frame.header.command = command;
    frame.header.data_length = (uint16_t)payload_len;
    write_u16_le(&frame.header.address_l, address);
    if (payload != NULL && payload_len > 0u) {
        memcpy(frame.header.data, payload, payload_len);
    }
    return frame;
}

void picboot_build_version_payload(const picboot_bootloader_t *bootloader, picboot_version_payload_t *payload) {
    if (bootloader == NULL || payload == NULL) {
        return;
    }
    payload->minor_version = bootloader->metadata.minor_version;
    payload->major_version = bootloader->metadata.major_version;
    payload->max_packet_size = bootloader->metadata.max_packet_size;
    payload->firmware_checksum = bootloader->metadata.firmware_checksum;
    payload->device_id = bootloader->metadata.device_id;
    payload->flash_end = bootloader->metadata.flash_end;
    payload->erase_blocksize = bootloader->metadata.erase_blocksize;
    payload->write_blocksize = bootloader->metadata.write_blocksize;
    payload->user_id[0] = bootloader->metadata.user_id[0];
    payload->user_id[1] = bootloader->metadata.user_id[1];
    payload->user_id[2] = bootloader->metadata.user_id[2];
    payload->user_id[3] = bootloader->metadata.user_id[3];
}

void picboot_build_config_window(const picboot_bootloader_t *bootloader, uint16_t address, uint16_t length, uint8_t *out) {
    size_t start;
    size_t copy_len;

    if (bootloader == NULL || out == NULL) {
        return;
    }
    if ((uint32_t)address >= PICBOOT_CONFIG_SPACE_SIZE) {
        memset(out, 0, length);
        return;
    }
    start = address;
    copy_len = length;
    if ((start + copy_len) > PICBOOT_CONFIG_SPACE_SIZE) {
        copy_len = PICBOOT_CONFIG_SPACE_SIZE - start;
    }
    memcpy(out, &bootloader->config_space[start], copy_len);
    if (copy_len < (size_t)length) {
        memset(out + copy_len, 0, (size_t)length - copy_len);
    }
}

uint16_t picboot_checksum_words_le(const uint8_t *data, size_t len) {
    if (data == NULL) {
        return 0u;
    }
    return picboot_checksum_words_le_impl(data, len);
}

uint16_t picboot_crc16_ccitt(const uint8_t *data, size_t len) {
    if (data == NULL) {
        return 0u;
    }
    return picboot_crc16_ccitt_impl(data, len);
}

static bool handle_read_version(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    picboot_version_payload_t payload;

    picboot_build_version_payload(bootloader, &payload);
    set_success_response(request, response, (const uint8_t *)&payload, sizeof(payload));
    return true;
}

static bool handle_read_flash(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address_words;
    size_t offset;

    length = picboot_request_u16(request);
    address_words = picboot_request_address(request);
    if (length > sizeof(response->header.data) || !picboot_flash_access_ok(address_words, length, false)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    offset = picboot_flash_offset(address_words);
    set_success_response(request, response, &bootloader->flash[offset], length);
    return true;
}

static bool handle_write_flash(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address_words;
    size_t offset;

    length = picboot_request_u16(request);
    address_words = picboot_request_address(request);
    if (!picboot_request_has_unlock(request)) {
        set_error_response(request, response, PICBOOT_ERROR_UNLOCK_FAILED);
        return false;
    }
    if (length > sizeof(request->header.data) || !picboot_flash_access_ok(address_words, length, true)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    offset = picboot_flash_offset(address_words);
    memcpy(&bootloader->flash[offset], request->header.data, length);
    set_success_response(request, response, (const uint8_t[]){PICBOOT_COMMAND_SUCCESS}, 1u);
    return true;
}

static bool handle_erase_flash(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t blocks;
    uint16_t address_words;
    size_t offset;
    size_t length_bytes;
    size_t idx;

    blocks = picboot_request_u16(request);
    address_words = picboot_request_address(request);
    if (!picboot_request_has_unlock(request)) {
        set_error_response(request, response, PICBOOT_ERROR_UNLOCK_FAILED);
        return false;
    }
    length_bytes = (size_t)blocks * PICBOOT_ERASE_FLASH_BLOCKSIZE;
    if (!picboot_flash_access_ok(address_words, length_bytes, true)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    offset = picboot_flash_offset(address_words);
    for (idx = 0u; idx < length_bytes && idx < PICBOOT_FLASH_BYTES; idx += 2u) {
        bootloader->flash[offset + idx] = 0xFFu;
        if (offset + idx + 1u < sizeof(bootloader->flash)) {
            bootloader->flash[offset + idx + 1u] = 0x3Fu;
        }
    }
    set_success_response(request, response, (const uint8_t[]){PICBOOT_COMMAND_SUCCESS}, 1u);
    return true;
}

static bool handle_read_ee_data(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address;

    length = picboot_request_u16(request);
    address = picboot_request_address(request);
    if (length > sizeof(response->header.data) || !picboot_range_within(address, length, sizeof(bootloader->ee_data))) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    set_success_response(request, response, &bootloader->ee_data[address], length);
    return true;
}

static bool handle_write_ee_data(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address;

    length = picboot_request_u16(request);
    address = picboot_request_address(request);
    if (!picboot_request_has_unlock(request)) {
        set_error_response(request, response, PICBOOT_ERROR_UNLOCK_FAILED);
        return false;
    }
    if (length > sizeof(request->header.data) || !picboot_range_within(address, length, sizeof(bootloader->ee_data))) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    memcpy(&bootloader->ee_data[address], request->header.data, length);
    set_success_response(request, response, (const uint8_t[]){PICBOOT_COMMAND_SUCCESS}, 1u);
    return true;
}

static bool handle_read_config(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address;
    uint8_t buffer[PICBOOT_FRAME_MAX_LEN];

    length = picboot_request_u16(request);
    address = picboot_request_address(request);
    if (length > sizeof(response->header.data) ||
        !picboot_range_within(address, length, PICBOOT_CONFIG_SPACE_SIZE)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }
    memset(buffer, 0, sizeof(buffer));
    picboot_build_config_window(bootloader, address, length, buffer);
    set_success_response(request, response, buffer, length);
    return true;
}

static bool handle_write_config(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address;

    length = picboot_request_u16(request);
    address = picboot_request_address(request);
    if (!picboot_request_has_unlock(request)) {
        set_error_response(request, response, PICBOOT_ERROR_UNLOCK_FAILED);
        return false;
    }
    if (length > sizeof(request->header.data) ||
        !picboot_range_within(address, length, PICBOOT_CONFIG_SPACE_SIZE)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    memcpy(&bootloader->config_space[address], request->header.data, length);
    set_success_response(request, response, (const uint8_t[]){PICBOOT_COMMAND_SUCCESS}, 1u);
    return true;
}

static bool handle_reset_device(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint8_t status;

    if (bootloader != NULL) {
        bootloader->parser.reset_requested = true;
        bootloader->application_running = true;
        bootloader->reset_counter++;
        bootloader->parser.session_synced = false;
        bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
    }
    status = PICBOOT_COMMAND_SUCCESS;
    set_success_response(request, response, &status, 1u);
    return true;
}

static bool handle_calc_checksum(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address_words;
    uint16_t check_sum;
    size_t offset;

    picboot_frame_clear(response);
    length = picboot_request_u16(request);
    address_words = picboot_request_address(request);
    if (!picboot_flash_access_ok(address_words, length, false)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    offset = picboot_flash_offset(address_words);
    check_sum = picboot_checksum_words_le(&bootloader->flash[offset], length);
    response->header.command = request->header.command;
    response->header.data_length = 2u;
    response->header.address_l = request->header.address_l;
    response->header.address_h = request->header.address_h;
    response->header.address_u = request->header.address_u;
    response->header.data[0] = (uint8_t)(check_sum & 0xFFu);
    response->header.data[1] = (uint8_t)((check_sum >> 8) & 0xFFu);
    return true;
}

static bool handle_calc_crc(const picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint16_t length;
    uint16_t address_words;
    uint16_t crc;
    size_t offset;

    picboot_frame_clear(response);
    length = picboot_request_u16(request);
    address_words = picboot_request_address(request);
    if (!picboot_flash_access_ok(address_words, length, false)) {
        set_error_response(request, response, PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE);
        return false;
    }

    offset = picboot_flash_offset(address_words);
    crc = picboot_crc16_ccitt(&bootloader->flash[offset], length);
    response->header.command = request->header.command;
    response->header.data_length = 2u;
    response->header.address_l = request->header.address_l;
    response->header.address_h = request->header.address_h;
    response->header.address_u = request->header.address_u;
    response->header.data[0] = (uint8_t)(crc & 0xFFu);
    response->header.data[1] = (uint8_t)((crc >> 8) & 0xFFu);
    return true;
}

bool picboot_bootloader_process_request(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response) {
    uint8_t error_status;

    if (bootloader == NULL || request == NULL || response == NULL) {
        return false;
    }

    bootloader->parser.rx_frames++;
    if (!picboot_request_is_structurally_valid(request, &error_status)) {
        bootloader->parser.parse_errors++;
        set_error_response(request, response, error_status);
        bootloader->parser.tx_frames++;
        return false;
    }

    switch (request->header.command) {
    case PICBOOT_READ_VERSION:
        bootloader->parser.tx_frames++;
        return handle_read_version(bootloader, request, response);
    case PICBOOT_READ_FLASH:
        bootloader->parser.tx_frames++;
        return handle_read_flash(bootloader, request, response);
    case PICBOOT_WRITE_FLASH:
        bootloader->parser.tx_frames++;
        return handle_write_flash(bootloader, request, response);
    case PICBOOT_ERASE_FLASH:
        bootloader->parser.tx_frames++;
        return handle_erase_flash(bootloader, request, response);
    case PICBOOT_READ_EE_DATA:
        bootloader->parser.tx_frames++;
        return handle_read_ee_data(bootloader, request, response);
    case PICBOOT_WRITE_EE_DATA:
        bootloader->parser.tx_frames++;
        return handle_write_ee_data(bootloader, request, response);
    case PICBOOT_READ_CONFIG:
        bootloader->parser.tx_frames++;
        return handle_read_config(bootloader, request, response);
    case PICBOOT_WRITE_CONFIG:
        bootloader->parser.tx_frames++;
        return handle_write_config(bootloader, request, response);
    case PICBOOT_CALC_CHECKSUM:
        bootloader->parser.tx_frames++;
        return handle_calc_checksum(bootloader, request, response);
    case PICBOOT_CALC_CRC:
        bootloader->parser.tx_frames++;
        return handle_calc_crc(bootloader, request, response);
    case PICBOOT_RESET_DEVICE:
        bootloader->parser.tx_frames++;
        return handle_reset_device(bootloader, request, response);
    default:
        bootloader->parser.tx_frames++;
        set_error_response(request, response, PICBOOT_ERROR_INVALID_COMMAND);
        return false;
    }
}

picboot_feed_result_t picboot_bootloader_feed(picboot_bootloader_t *bootloader, uint8_t byte, picboot_frame_t *response) {
    uint16_t expected_payload_len;

    if (bootloader == NULL) {
        return PICBOOT_FEED_ERROR;
    }

    switch (bootloader->parser.state) {
    case PICBOOT_PARSER_WAIT_STX:
        if (byte != PICBOOT_STX) {
            bootloader->parser.sync_loss_bytes++;
            return PICBOOT_FEED_NEED_MORE;
        }
        picboot_frame_clear(&bootloader->parser.current);
        bootloader->parser.header_bytes = 0u;
        bootloader->parser.payload_bytes = 0u;
        bootloader->parser.payload_expected = 0u;
        bootloader->parser.session_synced = true;
        bootloader->parser.sync_count++;
        bootloader->parser.state = PICBOOT_PARSER_READ_HEADER;
        return PICBOOT_FEED_NEED_MORE;

    case PICBOOT_PARSER_READ_HEADER:
        bootloader->parser.current.raw[bootloader->parser.header_bytes++] = byte;
        if (bootloader->parser.header_bytes < PICBOOT_FRAME_HEADER_LEN) {
            return PICBOOT_FEED_NEED_MORE;
        }
        expected_payload_len = (uint16_t)picboot_expected_request_payload_len(
            bootloader->parser.current.header.command,
            read_u16_le((const uint8_t *)&bootloader->parser.current.header.data_length));
        if (expected_payload_len > sizeof(bootloader->parser.current.header.data)) {
            bootloader->parser.parse_errors++;
            bootloader->parser.session_synced = false;
            bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
            return PICBOOT_FEED_ERROR;
        }
        bootloader->parser.payload_expected = expected_payload_len;
        if (expected_payload_len == 0u) {
            if (response == NULL) {
                bootloader->parser.parse_errors++;
                bootloader->parser.session_synced = false;
                bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
                return PICBOOT_FEED_ERROR;
            }
            if (!picboot_bootloader_process_request(bootloader, &bootloader->parser.current, response)) {
                bootloader->parser.session_synced = false;
                bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
                return PICBOOT_FEED_ERROR;
            }
            bootloader->parser.session_synced = false;
            bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
            return PICBOOT_FEED_FRAME_READY;
        }
        bootloader->parser.state = PICBOOT_PARSER_READ_PAYLOAD;
        return PICBOOT_FEED_NEED_MORE;

    case PICBOOT_PARSER_READ_PAYLOAD:
        if (bootloader->parser.payload_bytes >= PICBOOT_WRITE_FLASH_BLOCKSIZE * 2u) {
            bootloader->parser.parse_errors++;
            bootloader->parser.session_synced = false;
            bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
            return PICBOOT_FEED_ERROR;
        }
        bootloader->parser.current.header.data[bootloader->parser.payload_bytes++] = byte;
        if (bootloader->parser.payload_bytes < bootloader->parser.payload_expected) {
            return PICBOOT_FEED_NEED_MORE;
        }
        if (response == NULL) {
            bootloader->parser.parse_errors++;
            bootloader->parser.session_synced = false;
            bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
            return PICBOOT_FEED_ERROR;
        }
        if (!picboot_bootloader_process_request(bootloader, &bootloader->parser.current, response)) {
            bootloader->parser.session_synced = false;
            bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
            return PICBOOT_FEED_ERROR;
        }
        bootloader->parser.session_synced = false;
        bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
        return PICBOOT_FEED_FRAME_READY;
    }

    bootloader->parser.parse_errors++;
    bootloader->parser.session_synced = false;
    bootloader->parser.state = PICBOOT_PARSER_WAIT_STX;
    return PICBOOT_FEED_ERROR;
}
