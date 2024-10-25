//  Copyright (c) 2017-present, Qihoo, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef SRC_CODING_H_
#define SRC_CODING_H_

#undef STORAGE_PLATFORM_IS_LITTLE_ENDIAN

#if defined(__APPLE__)
#  include <machine/endian.h>  // __BYTE_ORDER
#  define __BYTE_ORDER __DARWIN_BYTE_ORDER
#  define __LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
#elif defined(__FreeBSD__)
#  include <sys/endian.h>
#  include <sys/types.h>
#  define STORAGE_PLATFORM_IS_LITTLE_ENDIAN (_BYTE_ORDER == _LITTLE_ENDIAN)
#else
#  include <endian.h>  // __BYTE_ORDER
#endif

#ifndef STORAGE_PLATFORM_IS_LITTLE_ENDIAN
#  define STORAGE_PLATFORM_IS_LITTLE_ENDIAN (__BYTE_ORDER == __LITTLE_ENDIAN)
#endif
#include <cstring>

namespace storage {
static const bool kLittleEndian = STORAGE_PLATFORM_IS_LITTLE_ENDIAN;
#undef STORAGE_PLATFORM_IS_LITTLE_ENDIAN

inline void EncodeFixed8(char* buf, uint8_t value) {
  if (kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
  }
}

inline void EncodeFixed16(char* buf, uint16_t value) {
  if (kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
  }
}

inline void EncodeFixed32(char* buf, uint32_t value) {
  if (kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
  }
}

inline void EncodeFixed64(char* buf, uint64_t value) {
  if (kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
    buf[4] = (value >> 32) & 0xff;
    buf[5] = (value >> 40) & 0xff;
    buf[6] = (value >> 48) & 0xff;
    buf[7] = (value >> 56) & 0xff;
  }
}

inline uint8_t DecodeFixed8(const char* ptr) {
  if (kLittleEndian) {
    // Load the raw bytes
    uint8_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    return static_cast<uint8_t>(static_cast<unsigned char>(ptr[0]));
  }
}

inline uint16_t DecodeFixed16(const char* ptr) {
  if (kLittleEndian) {
    // Load the raw bytes
    uint16_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    return ((static_cast<uint16_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint16_t>(static_cast<unsigned char>(ptr[1])) << 8));
  }
}

inline uint32_t DecodeFixed32(const char* ptr) {
  if (kLittleEndian) {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

inline uint64_t DecodeFixed64(const char* ptr) {
  if (kLittleEndian) {
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
  } else {
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
  }
}

inline uint64_t EncodeDoubleToUInt64(double value) {
  uint64_t result = 0;

  __builtin_memcpy(&result, &value, sizeof(value));

  if ((result >> 63) == 1) {
    // signed bit would be zero
    result ^= 0xffffffffffffffff;
  } else {
    // signed bit would be one
    result |= 0x8000000000000000;
  }

  return result;
}

inline double DecodeDoubleFromUInt64(uint64_t value) {
  if ((value >> 63) == 0) {
    value ^= 0xffffffffffffffff;
  } else {
    value &= 0x7fffffffffffffff;
  }

  double result = 0;
  __builtin_memcpy(&result, &value, sizeof(result));

  return result;
}

inline void EncodeSizedString(std::string* str, std::string& value) {
  char buf[4];
  EncodeFixed32(buf, value.size());
  str->append(buf, sizeof(buf));
  str->append(value);
}

inline void DecodeSizedString(rocksdb::Slice* input, std::string* value) {
  uint32_t size = DecodeFixed32(input->data());
  *value = std::string(input->data() + 4, size);
  input->remove_prefix(size);
}

}  // namespace storage
#endif  // SRC_CODING_H_
