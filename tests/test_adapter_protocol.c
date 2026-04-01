#include "picfw/codec_enh.h"
#include "picfw/codec_ens.h"
#include "picfw/common.h"
#include "picfw/info.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *name, const char *message) {
  fprintf(stderr, "[FAIL] %s: %s\n", name, message);
  return 1;
}

static int expect_true(const char *name, picfw_bool_t condition, const char *message) {
  if (!condition) {
    return fail(name, message);
  }
  return 0;
}

static int test_enh_roundtrip(void) {
  const char *name = "enh_roundtrip";
  uint8_t encoded[2];
  picfw_enh_frame_t decoded;

  if (expect_true(name, picfw_enh_encode(PICFW_ENH_REQ_INIT, 0x55u, encoded, sizeof(encoded)) == 2u, "encode length")) {
    return 1;
  }
  if (expect_true(name, picfw_enh_decode(encoded[0], encoded[1], &decoded) == 0, "decode")) {
    return 1;
  }
  if (expect_true(name, decoded.command == PICFW_ENH_REQ_INIT, "command mismatch")) {
    return 1;
  }
  if (expect_true(name, decoded.data == 0x55u, "data mismatch")) {
    return 1;
  }
  return 0;
}

static int test_enh_short_form(void) {
  const char *name = "enh_short_form";
  picfw_enh_parser_t parser;
  picfw_enh_frame_t frame;

  picfw_enh_parser_init(&parser);
  if (expect_true(name, picfw_enh_parser_feed(&parser, 0x7Fu, &frame) == PICFW_ENH_PARSE_COMPLETE, "expected short-form completion")) {
    return 1;
  }
  if (expect_true(name, frame.command == PICFW_ENH_RES_RECEIVED, "command mismatch")) {
    return 1;
  }
  if (expect_true(name, frame.data == 0x7Fu, "data mismatch")) {
    return 1;
  }
  if (expect_true(name, picfw_enh_encode_received(0x7Fu, (uint8_t[2]){0}, 2u) == 1u, "received short-form should encode to one byte")) {
    return 1;
  }
  return 0;
}

static int test_ens_roundtrip(void) {
  const char *name = "ens_roundtrip";
  const uint8_t input[] = {0x11u, 0xA9u, 0xAAu, 0x22u};
  uint8_t encoded[16];
  uint8_t decoded[16];
  int decoded_len;

  size_t encoded_len = picfw_ens_encode(input, sizeof(input), encoded, sizeof(encoded));
  if (expect_true(name, encoded_len > sizeof(input), "encoded stream should expand")) {
    return 1;
  }

  decoded_len = picfw_ens_decode(encoded, encoded_len, decoded, sizeof(decoded));
  if (expect_true(name, decoded_len == (int)sizeof(input), "decoded length")) {
    return 1;
  }
  if (expect_true(name, memcmp(input, decoded, sizeof(input)) == 0, "roundtrip mismatch")) {
    return 1;
  }
  return 0;
}

static int test_info_parsers(void) {
  const char *name = "info_parsers";
  const uint8_t version_payload[] = {0x03u, 0x01u, 0x12u, 0x34u, 0x1Eu, 0x02u, 0xCAu, 0xFEu};
  const uint8_t reset_payload[] = {0x05u, 0x01u};
  picfw_adapter_version_t version;
  picfw_adapter_reset_info_t reset;

  if (expect_true(name, picfw_info_parse_version(version_payload, sizeof(version_payload), &version) == 0, "version parse")) {
    return 1;
  }
  if (expect_true(name, version.version == 0x03u, "version byte")) {
    return 1;
  }
  if (expect_true(name, version.features == 0x01u && version.supports_info == PICFW_TRUE, "feature bits")) {
    return 1;
  }
  if (expect_true(name, version.has_checksum == PICFW_TRUE && version.has_bootloader == PICFW_TRUE, "length gates")) {
    return 1;
  }
  if (expect_true(name, version.checksum == 0x1234u && version.bootloader_checksum == 0xCAFEu, "checksums")) {
    return 1;
  }
  if (expect_true(name, version.jumpers == 0x1Eu && version.is_wifi == PICFW_TRUE && version.is_ethernet == PICFW_TRUE, "jumper bits")) {
    return 1;
  }

  if (expect_true(name, picfw_info_parse_reset(reset_payload, sizeof(reset_payload), &reset) == 0, "reset parse")) {
    return 1;
  }
  if (expect_true(name, strcmp(reset.cause_name, "external_reset") == 0, "reset cause")) {
    return 1;
  }
  if (expect_true(name, reset.cause_code == 0x05u && reset.restart_count == 0x01u, "reset values")) {
    return 1;
  }
  return 0;
}

int main(void) {
  if (test_enh_roundtrip() != 0) {
    return 1;
  }
  if (test_enh_short_form() != 0) {
    return 1;
  }
  if (test_ens_roundtrip() != 0) {
    return 1;
  }
  if (test_info_parsers() != 0) {
    return 1;
  }
  printf("adapter protocol tests passed\n");
  return 0;
}
