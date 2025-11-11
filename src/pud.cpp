#include "pud.h"
#include "lzss.h"
#include <stdexcept>
#include <algorithm>

static inline uint16_t u16le(const uint8_t* b) {
    return (uint16_t)(b[0] | (b[1] << 8));
}
static inline uint32_t u32le(const uint8_t* b) {
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}
static inline void p16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}
static inline void p32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

PudFile ParsePUD(const std::vector<uint8_t>& b, const std::string& file_name) {
    uint32_t size = (uint32_t)b.size();
    uint16_t first0 = size >= 2 ? u16le(&b[0]) : 0;
    uint16_t first1 = size >= 4 ? u16le(&b[2]) : 0;
    std::vector<PudBlock> blocks;
    uint32_t off = 4;
    int idx = 0;
    while (off + 20 <= size) {
        uint16_t w = u16le(&b[off+0]);
        uint16_t h = u16le(&b[off+2]);
        uint16_t u1 = u16le(&b[off+4]);
        uint16_t u2 = u16le(&b[off+6]);
        uint16_t u3 = u16le(&b[off+8]);
        uint16_t u4 = u16le(&b[off+10]);
        uint32_t dsize = u32le(&b[off+12]);
        uint32_t csize = u32le(&b[off+16]);
        if (w == 0 || h == 0 || csize == 0) break;
        uint32_t data_off = off + 20;
        uint32_t data_end = data_off + csize;
        if (data_end > size) break;
        blocks.push_back(PudBlock{ idx, off, w, h, u1,u2,u3,u4, dsize, csize, data_off, data_end });
        off = data_end;
        idx += 1;
    }
    if (blocks.empty()) throw std::runtime_error("Nenhum bloco PUD válido encontrado.");
    return PudFile{ file_name, size, first0, first1, std::move(blocks) };
}

std::vector<uint8_t> BuildPUD_FromBlocks(const PudFile& tmpl,
                                         const std::vector<std::vector<uint8_t>>& block_datas,
                                         bool use_raw,
                                         int bucket_limit,
                                         int max_candidates,
                                         bool lazy_matching) {
    if (block_datas.size() != tmpl.blocks.size()) {
        throw std::runtime_error("Número de blocos fornecidos não bate com o template.");
    }
    std::vector<uint8_t> out;
    p16(out, tmpl.first0);
    p16(out, tmpl.first1);
    for (size_t i = 0; i < block_datas.size(); ++i) {
        const auto& blk = tmpl.blocks[i];
        const auto& data = block_datas[i];
        std::vector<uint8_t> comp;
        uint32_t dsize = 0, csize = 0;
        if (use_raw) {
            comp = CompressLZSS_PSX(data, bucket_limit, max_candidates, lazy_matching);
            dsize = (uint32_t)data.size();
            csize = (uint32_t)comp.size();
        } else {
            comp = data;
            dsize = blk.dsize;
            csize = (uint32_t)comp.size();
        }
        p16(out, blk.w); p16(out, blk.h);
        p16(out, blk.u1); p16(out, blk.u2); p16(out, blk.u3); p16(out, blk.u4);
        p32(out, dsize); p32(out, csize);
        out.insert(out.end(), comp.begin(), comp.end());
    }
    return out;
}
