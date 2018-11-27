/* Send blaming letters to @yrtimd */
#ifndef HASH_HPP
#define HASH_HPP

#include <lib/system/common.hpp>
#include "utils.hpp"

inline cs::Hash generateHash(const void* data, size_t length) {
  cs::Hash result;
  cscrypto::CalculateHash(result, reinterpret_cast<const uint8_t*>(data), length);
  return result;
}

template <>
inline uint16_t getHashIndex(const cs::Hash& hash) {
  constexpr const size_t border = HASH_LENGTH / 2;

  uint16_t result = 0;
  auto byte = reinterpret_cast<uint8_t*>(&result);

  for (size_t i = 0; i < border; ++i) {
    *byte ^= hash[i];
  }

  ++byte;

  for (size_t i = border; i < HASH_LENGTH; ++i) {
    *byte ^= hash[i];
  }

  return result;
}

#endif  // HASH_HPP
