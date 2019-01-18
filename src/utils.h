#ifndef _HNS_UTILS_H
#define _HNS_UTILS_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "blake2b.h"
#include "ledger.h"
#include "segwit-addr.h"

#define HNS_APP_NAME "Handshake"
#define HNS_MAX_INPUTS 15
#define HNS_MAX_PATH 5
#define HNS_MAX_PATH_LEN 4 * HNS_MAX_PATH + 1

typedef uint32_t hns_varint_t;

typedef struct hns_xpub_s {
  uint8_t code[32];
  uint8_t key[33];
} hns_xpub_t;

typedef struct hns_apdu_pubkey_ctx_s {
  uint8_t store[109];
  uint8_t store_len;
  uint8_t confirm_str[20];
  uint8_t part_str[13];
  uint8_t full_str[67];
  uint8_t full_str_len;
  uint8_t full_str_pos;
} hns_apdu_pubkey_ctx_t;

typedef struct hns_input_s {
  uint8_t prev[36];
  uint8_t val[8];
  uint8_t seq[4];
} hns_input_t;

typedef struct hns_apdu_signature_ctx_t {
  bool sign_ready;
  bool skip_input;
  hns_input_t ins[HNS_MAX_INPUTS];
  uint8_t ins_len;
  uint8_t outs_len;
  uint8_t ver[4];
  uint8_t prevs[32];
  uint8_t seqs[32];
  uint8_t outs[32];
  uint8_t txid[32];
  uint8_t locktime[4];
  uint8_t sig[73];
  uint8_t part_str[13];
  uint8_t full_str[65];
  uint8_t full_str_len;
  uint8_t full_str_pos;
} hns_apdu_signature_ctx_t;

typedef union {
  hns_apdu_pubkey_ctx_t pubkey;
  hns_apdu_signature_ctx_t signature;
} global_ctx_t;

extern global_ctx_t global;

static inline void
hns_bin2hex(uint8_t * hex, uint8_t * bin, uint8_t len) {
  static uint8_t const lookup[] = "0123456789abcdef";
  uint8_t i;

  for (i = 0; i < len; i++) {
    hex[2*i+0] = lookup[(bin[i]>>4) & 0x0f];
    hex[2*i+1] = lookup[(bin[i]>>0) & 0x0f];
  }

  hex[2*len] = '\0';
}

static inline void
hns_create_p2pkh_addr(char * hrp, uint8_t * pub, uint8_t * out) {
  uint8_t pkh[20];
  uint8_t addr[42];

  if (blake2b(pkh, 20, NULL, 0, pub, 33))
    THROW(EXCEPTION);

  if (!segwit_addr_encode(addr, hrp, 0, pkh, 20))
    THROW(EXCEPTION);

  memmove(out, addr, sizeof(addr));
}

static inline uint8_t
size_varint(hns_varint_t val) {
  if (val < 0xfd)
    return 1;

  if (val <= 0xffff)
    return 3;

  if (val <= 0xffffffff)
    return 5;

  return 0;
}

static inline uint8_t
size_varsize(size_t val) {
  return size_varint((hns_varint_t)val);
}

static inline bool
read_u8(uint8_t ** buf, uint8_t * len, uint8_t * u8) {
  if (*len < 1)
    return false;

  *u8 = (*buf)[0];
  *buf += 1;
  *len -= 1;

  return true;
}

static inline bool
read_u16(uint8_t ** buf, uint8_t * len, uint16_t * u16, bool be) {
  if (*len < 2)
    return false;

  if (be) {
    *u16 = 0;
    *u16 |= ((uint16_t) (*buf)[0]) << 8;
    *u16 |=  (uint16_t) (*buf)[1];
  } else {
    memmove(u16, *buf, 2);
  }

  *buf += 2;
  *len -= 2;

  return true;
}

static inline bool
read_u32(uint8_t ** buf, uint8_t * len, uint32_t * u32, bool be) {
  if (*len < 4)
    return false;

  if (be) {
    *u32 = 0;
    *u32 |= ((uint32_t) (*buf)[0]) << 24;
    *u32 |= ((uint32_t) (*buf)[1]) << 16;
    *u32 |= ((uint32_t) (*buf)[2]) << 8;
    *u32 |=  (uint32_t) (*buf)[3];
  } else {
    memmove(u32, *buf, 4);
  }

  *buf += 4;
  *len -= 4;

  return true;
}

static inline bool
read_varint(uint8_t ** buf, uint8_t * len, hns_varint_t * varint) {
  uint8_t prefix = (*buf)[0];
  *buf += 1;
  *len -= 1;

  switch (prefix) {
    case 0xff:
      return false;

    case 0xfe: {
      uint32_t v;

      if (!read_u32(buf, len, &v, false)) {
        *buf -= 1;
        *len += 1;
        return false;
      }

      if (v <= 0xffff) {
        *buf -= 5;
        *len += 5;
        return false;
      }

      *varint = v;
      break;
    }

    case 0xfd: {
      uint16_t v;

      if (!read_u16(buf, len, &v, false)) {
        *buf -= 1;
        *len += 1;
        return false;
      }

      if (v < 0xfd) {
        *buf -= 3;
        *len += 3;
        return false;
      }

      *varint = v;
      break;
    }

    default:
      *varint = prefix;
      break;
  }

  return true;
}

static inline bool
peek_varint(uint8_t ** buf, uint8_t * len, hns_varint_t * varint) {
  if (!read_varint(buf, len, varint))
    return false;

  uint8_t sz = size_varint(*varint);

  *buf -= sz;
  *len += sz;

  return true;
}

static inline bool
read_varsize(uint8_t ** buf, uint8_t * len, size_t * val) {
  hns_varint_t v;

  if (!read_varint(buf, len, &v))
    return false;

  if (v < 0) {
    uint8_t sz = size_varint(v);

    buf -= sz;
    len += sz;

    return false;
  }

  *val = v;

  return true;
}

static inline bool
read_bytes(uint8_t ** buf, uint8_t * len, uint8_t * out, size_t sz) {
  if (*len < sz)
    return false;

  memmove(out, *buf, sz);

  *buf += sz;
  *len -= sz;

  return true;
}

static inline bool
read_varbytes(
  uint8_t ** buf,
  uint8_t * len,
  uint8_t * out,
  size_t out_sz,
  size_t * out_len
) {
  size_t sz;
  size_t offset;

  if (!read_varsize(buf, len, &sz))
    return false;

  offset = size_varsize(sz);

  if (out_sz < sz) {
    *buf -= offset;
    *len += offset;
    return false;
  }

  if (!read_bytes(buf, len, out, sz)) {
    *buf -= offset;
    *len += offset;
    return false;
  }

  *out_len = sz;

  return true;
}

static inline bool
read_bip32_path(
  uint8_t ** buf,
  uint8_t * len,
  uint8_t * depth,
  uint32_t * path
) {
  if (*len < 1)
    return false;

  if (!read_u8(buf, len, depth))
    return false;

  if (*depth > HNS_MAX_PATH) {
    *buf -= 1;
    *len += 1;
    return false;
  }

  uint8_t i;

  for (i = 0; i < *depth; i++) {
    if (!read_u32(buf, len, &path[i], true)) {
      *buf -= 4 + (4 * i);
      *len += 4 + (4 * i);
      return false;
    }
  }

  return true;
}

static inline size_t
write_u8(uint8_t ** buf, uint8_t u8) {
  if (buf == NULL || *buf == NULL)
    return 0;

  (*buf)[0] = u8;
  *buf += 1;

  return 1;
}

static inline size_t
write_u16(uint8_t ** buf, uint16_t u16, bool be) {
  if (buf == NULL || *buf == NULL)
    return 0;

  if (be) {
    (*buf)[0] = (uint8_t)u16;
    (*buf)[1] = (uint8_t)(u16 >> 8);
  } else {
    memmove(*buf, &u16, 2);
  }

  *buf += 2;

  return 2;
}

static inline size_t
write_u32(uint8_t ** buf, uint32_t u32, bool be) {
  if (buf == NULL || *buf == NULL)
    return 0;

  if (be) {
    (*buf)[0] = (uint8_t)u32;
    (*buf)[1] = (uint8_t)(u32 >> 8);
    (*buf)[2] = (uint8_t)(u32 >> 16);
    (*buf)[3] = (uint8_t)(u32 >> 24);
  } else {
    memmove(*buf, &u32, 4);
  }

  *buf += 4;

  return 4;
}

static inline size_t
write_bytes(uint8_t ** buf, const uint8_t * bytes, size_t sz) {
  if (buf == NULL || *buf == NULL)
    return 0;

  memmove(*buf, bytes, sz);
  *buf += sz;

  return sz;
}

static inline size_t
write_varint(uint8_t ** buf, hns_varint_t val) {
  if (buf == NULL || *buf == NULL)
    return 0;

  if (val < 0xfd) {
    write_u8(buf, (uint8_t)val);
    return 1;
  }

  if (val <= 0xffff) {
    write_u8(buf, 0xfd);
    write_u16(buf, (uint16_t)val, false);
    return 3;
  }

  if (val <= 0xffffffff) {
    write_u8(buf, 0xfe);
    write_u32(buf, (uint32_t)val, false);
    return 5;
  }

  return 0;
}

static inline size_t
write_varsize(uint8_t ** buf, size_t val) {
  return write_varint(buf, (uint64_t)val);
}

static inline size_t
write_varbytes(uint8_t ** buf, const uint8_t * bytes, size_t sz) {
  if (buf == NULL || *buf == NULL)
    return 0;

  size_t s = 0;
  s += write_varsize(buf, sz);
  s += write_bytes(buf, bytes, sz);

  return s;
}
#endif
