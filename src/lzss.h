#pragma once
#include <cstdint>
#include <vector>
#include <deque>
#include <unordered_map>

// PS1/Macross-compatible LZSS (ring 0xFEE) compressor/decompressor.

std::vector<uint8_t> DecompressLZSS_PSX(const std::vector<uint8_t>& data, size_t out_len_hint = 0);
std::vector<uint8_t> CompressLZSS_PSX(const std::vector<uint8_t>& data,
                                      int bucket_limit = 128,
                                      int max_candidates = 256,
                                      bool lazy_matching = true);
