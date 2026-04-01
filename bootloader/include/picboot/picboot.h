#ifndef PICBOOT_PICBOOT_H
#define PICBOOT_PICBOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#  define PICBOOT_PACKED
#  pragma pack(push, 1)
#else
#  define PICBOOT_PACKED __attribute__((__packed__))
#endif

#define PICBOOT_STX 0x55u

#define PICBOOT_READ_VERSION 0u
#define PICBOOT_READ_FLASH 1u
#define PICBOOT_WRITE_FLASH 2u
#define PICBOOT_ERASE_FLASH 3u
#define PICBOOT_READ_EE_DATA 4u
#define PICBOOT_WRITE_EE_DATA 5u
#define PICBOOT_READ_CONFIG 6u
#define PICBOOT_WRITE_CONFIG 7u
#define PICBOOT_CALC_CHECKSUM 8u
#define PICBOOT_RESET_DEVICE 9u
#define PICBOOT_CALC_CRC 10u

#define PICBOOT_MINOR_VERSION 0x08u
#define PICBOOT_MAJOR_VERSION 0x00u
#define PICBOOT_DEVICE_ID 0x30B0u
#define PICBOOT_BOOTLOADER_CHECKSUM 0x0000u
#define PICBOOT_END_BOOT 0x0400u
#define PICBOOT_BOOT_REGION_BYTES (PICBOOT_END_BOOT * 2u)
#define PICBOOT_APPLICATION_ENTRY PICBOOT_END_BOOT
#define PICBOOT_END_FLASH 0x4000u
#define PICBOOT_FLASH_BYTES (PICBOOT_END_FLASH * 2u)
#define PICBOOT_ERASE_FLASH_BLOCKSIZE 32u
#define PICBOOT_WRITE_FLASH_BLOCKSIZE 32u
#define PICBOOT_EE_DATA_BYTES 256u

/*
 * Host-side transfer expectations from modern ebuspicloader tooling. These are
 * bootloader session rates and should not be confused with the application
 * runtime EUSART profiles recovered from combined.hex.
 */
#define PICBOOT_HOST_TRANSFER_BAUD_SLOW 115200u
#define PICBOOT_HOST_TRANSFER_BAUD_FAST 921600u

#define PICBOOT_FRAME_HEADER_LEN 9u
#define PICBOOT_FRAME_MAX_LEN (PICBOOT_FRAME_HEADER_LEN + (2u * PICBOOT_WRITE_FLASH_BLOCKSIZE))
#define PICBOOT_WIRE_FRAME_MAX_LEN (1u + PICBOOT_FRAME_MAX_LEN)
#define PICBOOT_CONFIG_SPACE_SIZE 512u

#define PICBOOT_COMMAND_SUCCESS 0x01u
#define PICBOOT_STATUS_UNIMPLEMENTED 0xFDu
#define PICBOOT_ERROR_UNLOCK_FAILED 0xFCu
#define PICBOOT_ERROR_ADDRESS_OUT_OF_RANGE 0xFEu
#define PICBOOT_ERROR_INVALID_COMMAND 0xFFu

typedef enum picboot_parser_state {
    PICBOOT_PARSER_WAIT_STX = 0,
    PICBOOT_PARSER_READ_HEADER = 1,
    PICBOOT_PARSER_READ_PAYLOAD = 2
} picboot_parser_state_t;

typedef enum picboot_feed_result {
    PICBOOT_FEED_NEED_MORE = 0,
    PICBOOT_FEED_FRAME_READY = 1,
    PICBOOT_FEED_ERROR = -1
} picboot_feed_result_t;

typedef struct PICBOOT_PACKED picboot_frame_header {
    uint8_t command;
    uint16_t data_length;
    uint8_t ee_key_1;
    uint8_t ee_key_2;
    uint8_t address_l;
    uint8_t address_h;
    uint8_t address_u;
    uint8_t address_unused;
    uint8_t data[2u * PICBOOT_WRITE_FLASH_BLOCKSIZE];
} picboot_frame_header_t;

typedef union picboot_frame {
    picboot_frame_header_t header;
    uint8_t raw[PICBOOT_FRAME_MAX_LEN];
} picboot_frame_t;

typedef struct picboot_version_payload {
    uint8_t minor_version;
    uint8_t major_version;
    uint16_t max_packet_size;
    uint16_t firmware_checksum;
    uint16_t device_id;
    uint16_t flash_end;
    uint8_t erase_blocksize;
    uint8_t write_blocksize;
    uint8_t user_id[4];
} picboot_version_payload_t;

typedef struct picboot_metadata {
    uint8_t minor_version;
    uint8_t major_version;
    uint16_t max_packet_size;
    uint16_t firmware_checksum;
    uint16_t device_id;
    uint16_t flash_end;
    uint8_t erase_blocksize;
    uint8_t write_blocksize;
    uint8_t user_id[4];
} picboot_metadata_t;

typedef struct picboot_parser {
    picboot_parser_state_t state;
    picboot_frame_t current;
    size_t header_bytes;
    size_t payload_bytes;
    uint16_t payload_expected;
    bool reset_requested;
    bool session_synced;
    uint32_t sync_count;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t parse_errors;
    uint32_t sync_loss_bytes;  /* bytes discarded while waiting for STX */
} picboot_parser_t;

/* NOTE: picboot_bootloader_t (~33KB) is a host-side memory model.
 * It contains full flash/EEPROM/config arrays for simulation.
 * On real PIC hardware, these are accessed via NVM registers, not RAM copies.
 * This struct MUST NOT be instantiated on the PIC target. */
typedef struct picboot_bootloader {
    picboot_metadata_t metadata;
    uint8_t flash[PICBOOT_FLASH_BYTES];
    uint8_t ee_data[PICBOOT_EE_DATA_BYTES];
    uint8_t config_space[PICBOOT_CONFIG_SPACE_SIZE];
    picboot_parser_t parser;
    bool application_running;
    uint16_t application_entry;
    uint32_t reset_counter;
} picboot_bootloader_t;

void picboot_frame_clear(picboot_frame_t *frame);
void picboot_metadata_init(picboot_metadata_t *metadata);
void picboot_bootloader_init(picboot_bootloader_t *bootloader);
void picboot_bootloader_init_with_metadata(picboot_bootloader_t *bootloader, const picboot_metadata_t *metadata);

size_t picboot_frame_serialize(const picboot_frame_t *frame, uint8_t *out, size_t out_capacity);
size_t picboot_frame_serialize_with_stx(const picboot_frame_t *frame, uint8_t *out, size_t out_capacity);
bool picboot_frame_deserialize(picboot_frame_t *frame, const uint8_t *data, size_t len);

bool picboot_deadline_reached_u32(uint32_t now, uint32_t deadline);
bool picboot_deadline_reached_u16(uint16_t now, uint16_t deadline);

size_t picboot_expected_request_payload_len(uint8_t command, uint16_t request_data_length);
picboot_feed_result_t picboot_bootloader_feed(picboot_bootloader_t *bootloader, uint8_t byte, picboot_frame_t *response);
bool picboot_bootloader_process_request(picboot_bootloader_t *bootloader, const picboot_frame_t *request, picboot_frame_t *response);

picboot_frame_t picboot_make_request(uint8_t command, uint16_t address, uint16_t data_length, const uint8_t *payload, size_t payload_len);
picboot_frame_t picboot_make_response(uint8_t command, uint16_t address, const uint8_t *payload, size_t payload_len);

void picboot_build_version_payload(const picboot_bootloader_t *bootloader, picboot_version_payload_t *payload);
void picboot_build_config_window(const picboot_bootloader_t *bootloader, uint16_t address, uint16_t length, uint8_t *out);
uint16_t picboot_checksum_words_le(const uint8_t *data, size_t len);
uint16_t picboot_crc16_ccitt(const uint8_t *data, size_t len);

#if defined(_MSC_VER)
#  pragma pack(pop)
#endif

#endif
