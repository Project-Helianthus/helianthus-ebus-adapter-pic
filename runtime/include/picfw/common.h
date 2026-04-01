#ifndef PICFW_COMMON_H
#define PICFW_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* picfw_bool_t: uint8_t-based boolean for PIC16F XC8 compatibility.
 * The bootloader module uses <stdbool.h> (C99 bool) because it targets
 * host-side simulation where compiler support is guaranteed. The runtime
 * module uses this typedef because older XC8 versions had quirks with _Bool
 * on PIC16F enhanced mid-range. Values are 0 (FALSE) or 1 (TRUE); other
 * values should be avoided. Test comparisons should use != PICFW_FALSE
 * rather than == PICFW_TRUE for robustness. */
typedef uint8_t picfw_bool_t;

#define PICFW_FALSE ((picfw_bool_t)0)
#define PICFW_TRUE ((picfw_bool_t)1)

typedef enum picfw_startup_state {
  PICFW_STARTUP_BOOT_INIT = 0,
  PICFW_STARTUP_CACHE_LOADED_STALE = 1,
  PICFW_STARTUP_LIVE_WARMUP = 2,
  PICFW_STARTUP_LIVE_READY = 3,
  PICFW_STARTUP_DEGRADED = 4,
} picfw_startup_state_t;

static inline picfw_bool_t picfw_deadline_reached_u32(uint32_t now_ms, uint32_t deadline_ms) {
  return (picfw_bool_t)((int32_t)(now_ms - deadline_ms) >= 0);
}

static inline uint8_t picfw_u8_min(uint8_t left, uint8_t right) {
  return left < right ? left : right;
}

static inline uint32_t picfw_rot32_right(uint32_t v, uint8_t n) {
  n &= 31u;
  if (n == 0u) {
    return v;
  }
  return (v >> n) | (v << (32u - n));
}

static inline uint32_t picfw_rot32_left(uint32_t v, uint8_t n) {
  n &= 31u;
  if (n == 0u) {
    return v;
  }
  return (v << n) | (v >> (32u - n));
}

#ifdef PICFW_DEBUG
#include <stdio.h>
#include <stdlib.h>  /* NOLINT(determinism) debug-only: abort() */
#define PICFW_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "PICFW_ASSERT failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)
#else
#define PICFW_ASSERT(cond) ((void)(cond))
#endif

#endif
