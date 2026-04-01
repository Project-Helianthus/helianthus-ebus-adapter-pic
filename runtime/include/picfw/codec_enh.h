#ifndef PICFW_CODEC_ENH_H
#define PICFW_CODEC_ENH_H

#include "common.h"

typedef enum picfw_enh_command {
  PICFW_ENH_REQ_INIT = 0x0,
  PICFW_ENH_REQ_SEND = 0x1,
  PICFW_ENH_REQ_START = 0x2,
  PICFW_ENH_REQ_INFO = 0x3,

  PICFW_ENH_RES_RESETTED = 0x0,
  PICFW_ENH_RES_RECEIVED = 0x1,
  PICFW_ENH_RES_STARTED = 0x2,
  PICFW_ENH_RES_INFO = 0x3,
  PICFW_ENH_RES_FAILED = 0xA,
  PICFW_ENH_RES_ERROR_EBUS = 0xB,
  PICFW_ENH_RES_ERROR_HOST = 0xC,
} picfw_enh_command_t;

typedef enum picfw_enh_parse_result {
  PICFW_ENH_PARSE_NEED_MORE = 0,
  PICFW_ENH_PARSE_COMPLETE = 1,
  PICFW_ENH_PARSE_ERROR = -1,
} picfw_enh_parse_result_t;

typedef struct picfw_enh_frame {
  uint8_t command;
  uint8_t data;
} picfw_enh_frame_t;

typedef struct picfw_enh_parser {
  picfw_bool_t pending;
  uint8_t byte1;
} picfw_enh_parser_t;

void picfw_enh_parser_init(picfw_enh_parser_t *parser);
picfw_enh_parse_result_t picfw_enh_parser_feed(
    picfw_enh_parser_t *parser,
    uint8_t byte,
    picfw_enh_frame_t *frame_out);

size_t picfw_enh_encode_frame(const picfw_enh_frame_t *frame, uint8_t *out, size_t out_cap);
size_t picfw_enh_encode(uint8_t command, uint8_t data, uint8_t *out, size_t out_cap);
size_t picfw_enh_encode_received(uint8_t data, uint8_t *out, size_t out_cap);
int picfw_enh_decode(uint8_t byte1, uint8_t byte2, picfw_enh_frame_t *frame_out);

#endif
