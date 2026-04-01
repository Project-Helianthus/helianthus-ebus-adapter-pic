#ifndef PICFW_CODEC_ENS_H
#define PICFW_CODEC_ENS_H

#include "common.h"

#define PICFW_ENS_INPUT_MAX 256u

typedef struct picfw_ens_parser {
  uint8_t escape_pending;
} picfw_ens_parser_t;

void picfw_ens_parser_init(picfw_ens_parser_t *parser);
int picfw_ens_parser_feed(picfw_ens_parser_t *parser, uint8_t byte, uint8_t *decoded_byte);
size_t picfw_ens_encode(const uint8_t *input, size_t input_len, uint8_t *out, size_t out_cap);
int picfw_ens_decode(const uint8_t *input, size_t input_len, uint8_t *out, size_t out_cap);

#endif
