#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PudBlock {
    int idx;
    uint32_t hdr_off;
    uint16_t w, h;
    uint16_t u1, u2, u3, u4;
    uint32_t dsize;
    uint32_t csize;
    uint32_t data_off;
    uint32_t data_end;
};

struct PudFile {
    std::string path;
    uint32_t size;
    uint16_t first0;
    uint16_t first1;
    std::vector<PudBlock> blocks;
};

PudFile ParsePUD(const std::vector<uint8_t>& bytes, const std::string& file_name);
std::vector<uint8_t> BuildPUD_FromBlocks(const PudFile& tmpl,
                                         const std::vector<std::vector<uint8_t>>& block_datas,
                                         bool use_raw,
                                         int bucket_limit = 128,
                                         int max_candidates = 256,
                                         bool lazy_matching = true);
