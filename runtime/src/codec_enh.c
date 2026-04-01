#include "picfw/codec_enh.h"

static uint8_t picfw_enh_is_short_form(uint8_t byte) {
  return (uint8_t)((byte & 0x80u) == 0u);
}

void picfw_enh_parser_init(picfw_enh_parser_t *parser) {
  if (parser == 0) {
    return;
  }
  parser->pending = 0u;
  parser->byte1 = 0u;
}

int picfw_enh_decode(uint8_t byte1, uint8_t byte2, picfw_enh_frame_t *frame_out) {
  if ((byte1 & 0xC0u) != 0xC0u) {
    return -1;
  }
  if ((byte2 & 0xC0u) != 0x80u) {
    return -1;
  }
  if (frame_out == 0) {
    return -1;
  }
  frame_out->command = (uint8_t)((byte1 >> 2) & 0x0Fu);
  frame_out->data = (uint8_t)(((byte1 & 0x03u) << 6) | (byte2 & 0x3Fu));
  return 0;
}

/* Returns 0 on error (null output, insufficient capacity) or when
 * the encoded length is 0. Callers treat 0 as "nothing to enqueue"
 * which is correct for both error and empty cases in this firmware. */
size_t picfw_enh_encode(uint8_t command, uint8_t data, uint8_t *out, size_t out_cap) {
  if (out == 0) {
    return 0;
  }
  if (out_cap < 2u) {
    return 0;
  }
  out[0] = (uint8_t)(0xC0u | ((command & 0x0Fu) << 2) | ((data & 0xC0u) >> 6));
  out[1] = (uint8_t)(0x80u | (data & 0x3Fu));
  return 2u;
}

size_t picfw_enh_encode_received(uint8_t data, uint8_t *out, size_t out_cap) {
  if (out == 0) {
    return 0;
  }
  if (!picfw_enh_is_short_form(data)) {
    return picfw_enh_encode(PICFW_ENH_RES_RECEIVED, data, out, out_cap);
  }
  if (out_cap < 1u) {
    return 0;
  }
  out[0] = data;
  return 1u;
}

size_t picfw_enh_encode_frame(const picfw_enh_frame_t *frame, uint8_t *out, size_t out_cap) {
  if (frame == 0) {
    return 0;
  }
  return picfw_enh_encode(frame->command, frame->data, out, out_cap);
}

picfw_enh_parse_result_t picfw_enh_parser_feed(
    picfw_enh_parser_t *parser,
    uint8_t byte,
    picfw_enh_frame_t *frame_out) {
  if (parser == 0 || frame_out == 0) {
    return PICFW_ENH_PARSE_ERROR;
  }

  if (!parser->pending) {
    if (picfw_enh_is_short_form(byte)) {
      frame_out->command = PICFW_ENH_RES_RECEIVED;
      frame_out->data = byte;
      return PICFW_ENH_PARSE_COMPLETE;
    }

    if ((byte & 0xC0u) == 0x80u) {
      return PICFW_ENH_PARSE_ERROR;
    }

    parser->pending = 1u;
    parser->byte1 = byte;
    return PICFW_ENH_PARSE_NEED_MORE;
  }

  if ((byte & 0xC0u) == 0x80u) {
    parser->pending = 0u;
    if (picfw_enh_decode(parser->byte1, byte, frame_out) != 0) {
      return PICFW_ENH_PARSE_ERROR;
    }
    return PICFW_ENH_PARSE_COMPLETE;
  }

  if (byte >= 0xC0u) {
    parser->byte1 = byte;
    return PICFW_ENH_PARSE_NEED_MORE;
  }

  parser->pending = 0u;
  frame_out->command = PICFW_ENH_RES_RECEIVED;
  frame_out->data = byte;
  return PICFW_ENH_PARSE_COMPLETE;
}
