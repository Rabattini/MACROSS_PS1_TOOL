#include "lzss.h"
#include <algorithm>
#include <stdexcept>

namespace {
    constexpr int WINDOW_SIZE = 4096;
    constexpr int RING_INIT   = 0xFEE;
    constexpr int MIN_MATCH   = 3;
    constexpr int MAX_MATCH   = 18;
    constexpr int HASH_LEN    = 3;

    static inline uint32_t hash3(const uint8_t* b) {
        return (uint32_t)((b[0] * 0x1F1F) + (b[1] * 0x1F) + b[2]);
    }

    struct IndexBucket {
        std::deque<int> pos;
    };

    using IndexMap = std::unordered_map<uint32_t, IndexBucket>;

    static inline void add_pos(IndexMap& index, const std::vector<uint8_t>& src, int n, int j, int bucket_limit) {
        if (j < 0 || j + HASH_LEN > n) return;
        uint32_t key = hash3(&src[j]);
        auto& bucket = index[key].pos;
        int win_min = j - WINDOW_SIZE;
        while (!bucket.empty() && bucket.front() <= win_min) bucket.pop_front();
        bucket.push_back(j);
        while ((int)bucket.size() > bucket_limit) bucket.pop_front();
    }

    static inline std::pair<int,int> find_best(const std::vector<uint8_t>& src, int i, int n,
                                               IndexMap& index, int max_candidates) {
        int remaining = n - i;
        if (remaining < MIN_MATCH) return {0,0};
        uint32_t key = hash3(&src[i]);
        auto it = index.find(key);
        if (it == index.end() || it->second.pos.empty()) return {0,0};
        const auto& bucket = it->second.pos;
        int best_len = 0;
        int best_back = 0;
        int checked = 0;
        for (auto rit = bucket.rbegin(); rit != bucket.rend(); ++rit) {
            int pos = *rit;
            if (pos < i - WINDOW_SIZE || pos >= i) continue;
            int max_len = std::min(MAX_MATCH, n - i);
            int l = MIN_MATCH;
            while (l < max_len && src[pos + l] == src[i + l]) {
                ++l;
            }
            if (l > best_len) {
                best_len = l;
                best_back = i - pos;
                if (best_len == MAX_MATCH) break;
            }
            checked++;
            if (checked >= max_candidates) break;
        }
        return {best_len, best_back};
    }
} // namespace

std::vector<uint8_t> DecompressLZSS_PSX(const std::vector<uint8_t>& data, size_t out_len_hint) {
    std::vector<uint8_t> out;
    out.reserve(out_len_hint ? out_len_hint : data.size() * 4 + 64);

    std::vector<uint8_t> dict(0x1000, 0);
    int dict_pos = RING_INIT;
    size_t src = 0, n = data.size();

    auto getb = [&]() -> uint8_t {
        if (src >= n) throw std::runtime_error("EOF during LZSS read");
        return data[src++];
    };

    while (true) {
        if (out_len_hint && out.size() >= out_len_hint) break;
        if (src >= n) break;
        uint8_t flags = getb();
        uint8_t mask = 0x80;
        while (mask) {
            if (out_len_hint && out.size() >= out_len_hint) break;
            if (src >= n) break;
            if ((flags & mask) == 0) {
                uint8_t b = getb();
                out.push_back(b);
                dict[dict_pos] = b;
                dict_pos = (dict_pos + 1) & 0xFFF;
            } else {
                if (src + 1 > n) return out;
                uint8_t b1 = getb();
                if (src >= n) return out;
                uint8_t b2 = getb();
                int length = (b2 & 0x0F) + 3;
                int off = ((b2 & 0xF0) << 4) | b1;
                for (int i = 0; i < length; ++i) {
                    uint8_t v = dict[(off + i) & 0x0FFF];
                    out.push_back(v);
                    dict[dict_pos] = v;
                    dict_pos = (dict_pos + 1) & 0x0FFF;
                }
            }
            mask >>= 1;
        }
    }
    return out;
}

std::vector<uint8_t> CompressLZSS_PSX(const std::vector<uint8_t>& data,
                                      int bucket_limit,
                                      int max_candidates,
                                      bool lazy_matching) {
    const int n = (int)data.size();
    if (n == 0) return {};

    std::vector<uint8_t> out;
    out.reserve(data.size() / 2 + 64);

    auto start_group = [&](uint8_t& control, int& bits, size_t& control_pos) {
        control = 0;
        bits = 0;
        out.push_back(0);
        control_pos = out.size() - 1;
    };
    auto flush_group = [&](uint8_t control, size_t control_pos) {
        out[control_pos] = control;
    };

    IndexMap index;
    int ring_pos = RING_INIT;
    int i = 0;

    uint8_t control = 0;
    int bits = 0;
    size_t control_pos = 0;
    start_group(control, bits, control_pos);

    auto add_up_to = [&](int from, int count) {
        int limit = std::min(from + count, n - (HASH_LEN - 1));
        for (int j = from; j < limit; ++j) add_pos(index, data, n, j, bucket_limit);
    };

    while (i < n) {
        auto [best_len, best_back] = find_best(data, i, n, index, max_candidates);

        if (lazy_matching && best_len == 3 && i + 1 < n) {
            auto fb_next = find_best(data, i + 1, n, index, max_candidates);
            if (fb_next.first >= 4) {
                // Emit literal
                out.push_back(data[i]);
                ring_pos = (ring_pos + 1) & 0x0FFF;
                if (i <= n - HASH_LEN) add_pos(index, data, n, i, bucket_limit);
                bits += 1;
                if (bits == 8) {
                    flush_group(control, control_pos);
                    if (i + 1 < n) start_group(control, bits, control_pos);
                }
                ++i;
                continue;
            }
        }

        if (best_len >= MIN_MATCH) {
            control |= (uint8_t)(0x80 >> bits);
            int distance = (ring_pos - best_back) & 0x0FFF;
            int length = best_len;
            out.push_back((uint8_t)(distance & 0xFF));
            out.push_back((uint8_t)(((distance >> 8) & 0x0F) << 4 | ((length - 3) & 0x0F)));
            ring_pos = (ring_pos + length) & 0x0FFF;
            add_up_to(i, length);
            i += length;
        } else {
            out.push_back(data[i]);
            ring_pos = (ring_pos + 1) & 0x0FFF;
            if (i <= n - HASH_LEN) add_pos(index, data, n, i, bucket_limit);
            ++i;
        }

        bits += 1;
        if (bits == 8) {
            flush_group(control, control_pos);
            if (i < n) start_group(control, bits, control_pos);
        }
    }
    if (bits != 0) {
        out[control_pos] = control;
    }
    return out;
}
