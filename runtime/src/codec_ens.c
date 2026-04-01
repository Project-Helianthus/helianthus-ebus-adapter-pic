#include "picfw/codec_ens.h"

enum {
  PICFW_ENS_ESCAPE = 0xA9u,
  PICFW_ENS_SYNC = 0xAAu,
  PICFW_ENS_ESC_ESCAPE = 0x00u,
  PICFW_ENS_ESC_SYNC = 0x01u,
};

void picfw_ens_parser_init(picfw_ens_parser_t *parser) {
  if (parser == 0) {
    return;
  }
  parser->escape_pending = 0u;
}

int picfw_ens_parser_feed(picfw_ens_parser_t *parser, uint8_t byte, uint8_t *decoded_byte) {
  if (parser == 0 || decoded_byte == 0) {
    return -1;
  }

  if (parser->escape_pending) {
    parser->escape_pending = 0u;
    if (byte == PICFW_ENS_ESC_ESCAPE) {
      *decoded_byte = PICFW_ENS_ESCAPE;
      return 1;
    }
    if (byte == PICFW_ENS_ESC_SYNC) {
      *decoded_byte = PICFW_ENS_SYNC;
      return 1;
    }
    return -1;
  }

  if (byte == PICFW_ENS_ESCAPE) {
    parser->escape_pending = 1u;
    return 0;
  }

  if (byte == PICFW_ENS_SYNC) {
    return -1;
  }

  *decoded_byte = byte;
  return 1;
}

/* Encode a single byte into the output buffer. Returns the number of bytes
 * written (1 or 2), or 0 on overflow. */
static size_t ens_encode_byte(uint8_t byte, uint8_t *out, size_t out_cap,
                              size_t out_pos) {
  if (byte == PICFW_ENS_ESCAPE || byte == PICFW_ENS_SYNC) {
    if (out_pos + 2u > out_cap) {
      return 0;
    }
    out[out_pos] = PICFW_ENS_ESCAPE;
    out[out_pos + 1u] = (byte == PICFW_ENS_ESCAPE) ? PICFW_ENS_ESC_ESCAPE
                                                    : PICFW_ENS_ESC_SYNC;
    return 2;
  }
  if (out_pos + 1u > out_cap) {
    return 0;
  }
  out[out_pos] = byte;
  return 1;
}

/* Returns 0 on error (null output, insufficient capacity) or when
 * the encoded length is 0. Callers treat 0 as "nothing to enqueue"
 * which is correct for both error and empty cases in this firmware. */
size_t picfw_ens_encode(const uint8_t *input, size_t input_len, uint8_t *out, size_t out_cap) {
  size_t out_len = 0;
  size_t idx;

  if (input == 0 || out == 0) {
    return 0;
  }
  if (input_len > PICFW_ENS_INPUT_MAX) {
    input_len = PICFW_ENS_INPUT_MAX;
  }

  for (idx = 0; idx < input_len && idx < PICFW_ENS_INPUT_MAX; ++idx) {
    size_t written = ens_encode_byte(input[idx], out, out_cap, out_len);
    if (written == 0) {
      return 0;
    }
    out_len += written;
  }

  return out_len;
}

/* Process one decoded byte from the parser, appending to the output buffer.
 * Returns 0 on success, -1 on overflow. */
static int ens_decode_store(uint8_t decoded, uint8_t *out, size_t out_cap,
                            size_t *out_len) {
  if (*out_len >= out_cap) {
    return -1;
  }
  out[(*out_len)++] = decoded;
  return 0;
}

/* Returns decoded byte count as int. Safe because input is bounded
 * by FIFO capacity (< 256 bytes) and each input byte produces at most
 * one output byte. Overflow of int is impossible for valid inputs. */
int picfw_ens_decode(const uint8_t *input, size_t input_len, uint8_t *out, size_t out_cap) {
  picfw_ens_parser_t parser;
  size_t out_len = 0;
  size_t idx;

  if (input == 0 || out == 0) {
    return -1;
  }
  if (input_len > PICFW_ENS_INPUT_MAX) {
    input_len = PICFW_ENS_INPUT_MAX;
  }

  picfw_ens_parser_init(&parser);
  for (idx = 0; idx < input_len; ++idx) {
    uint8_t decoded = 0;
    int rc = picfw_ens_parser_feed(&parser, input[idx], &decoded);
    if (rc < 0) {
      return -1;
    }
    if (rc == 1) {
      if (ens_decode_store(decoded, out, out_cap, &out_len) < 0) {
        return -1;
      }
    }
  }

  if (parser.escape_pending) {
    return -1;
  }

  return (int)out_len;
}
