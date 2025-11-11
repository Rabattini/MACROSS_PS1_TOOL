#include "gko.h"
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <array>
#include <windows.h>
#include <unordered_map>
#include <cctype>

static inline uint16_t u16le(const uint8_t* b) {
    return (uint16_t)(b[0] | (b[1] << 8));
}
static inline uint32_t u32le(const uint8_t* b) {
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}
static inline void p32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

std::vector<GkoEntry> ParseGKO(const std::vector<uint8_t>& b) {
    if (b.size() < 4) throw std::runtime_error("GKO inválido (tamanho insuficiente)");
    uint32_t count = u32le(&b[0]);
    size_t toc_offset = 4;
    std::vector<GkoEntry> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t entry_off = toc_offset + i * 24;
        if (entry_off + 24 > b.size()) throw std::runtime_error("TOC excede tamanho do arquivo");
        std::array<uint8_t,16> name_raw{};
        std::copy_n(&b[entry_off], 16, name_raw.begin());
        std::string name;
        for (int k=0;k<16;k++){ if (name_raw[k]==0) break; name.push_back((char)name_raw[k]); }
        uint32_t off = u32le(&b[entry_off+16]);
        uint32_t size = u32le(&b[entry_off+20]);
        if ((size_t)off + size > b.size()) throw std::runtime_error("Entrada fora dos limites");
        std::vector<uint8_t> data(&b[off], &b[off+size]);
        out.push_back(GkoEntry{ name, off, size, std::move(data), name_raw });
    }
    return out;
}

int DetectGKOAlignment(const std::vector<GkoEntry>& entries) {
    if (entries.empty()) return 1;
    auto allAligned = [&](int a)->bool {
        for (auto& e: entries) { if (e.offset % a != 0) return false; }
        return true;
    };
    const int candidates[] = {0x800,0x400,0x200,0x100,0x80,0x40,0x20,0x10,0x08,0x04,0x02};
    for (int a: candidates) if (allAligned(a)) return a;
    return 1;
}

static inline size_t AlignUp(size_t x, size_t a) {
    if (a <= 1) return x;
    return ((x + a - 1) / a) * a;
}

static inline std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Falha ao abrir arquivo: " + p.string());
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(sz);
    f.read((char*)buf.data(), sz);
    return buf;
}

std::vector<uint8_t> BuildGKO_PreserveOrder(const std::vector<GkoEntry>& orderEntries,
                                            const std::filesystem::path& folder) {
    if (orderEntries.empty())
        throw std::runtime_error("Arquivo .GKO original não carregado.");

    int align = DetectGKOAlignment(orderEntries);
    const size_t N = orderEntries.size();
    const size_t header_size = 4 + N * 24;

    std::vector<uint8_t> toc;
    std::vector<uint8_t> data_blob;

    size_t cur_off = AlignUp(header_size, (size_t)align);

    auto lower = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return s; };

    // Build a file list
    std::vector<std::filesystem::path> files;
    for (auto const& entry: std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }

    auto Latin1ToWide = [](const std::string& s)->std::wstring{
        std::wstring w; w.reserve(s.size());
        for (unsigned char c: s) w.push_back((wchar_t)c);
        return w;
    };
    auto EqualsIgnoreCase = [](const std::wstring& a, const std::wstring& b)->bool{
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
    };

    for (auto const& e: orderEntries) {
        std::filesystem::path chosen;
        std::wstring want = Latin1ToWide(e.name);

        // Try full filename (case-insensitive)
        for (auto const& p: files) {
            if (EqualsIgnoreCase(p.filename().wstring(), want)) { chosen = p; break; }
        }
        // Try by stem if not found
        if (chosen.empty()) {
            std::wstring wantStem = std::filesystem::path(want).stem().wstring();
            for (auto const& p: files) {
                if (EqualsIgnoreCase(p.stem().wstring(), wantStem)) { chosen = p; break; }
            }
        }
        if (chosen.empty()) {
            throw std::runtime_error(std::string("Arquivo correspondente a '") + e.name + "' não encontrado na pasta.");
        }
        auto bytes = ReadAllBytes(chosen);
        // name field (16 bytes) preserved
        std::array<uint8_t,16> name_field = e.name_raw;

        toc.insert(toc.end(), name_field.begin(), name_field.end());
        p32(toc, (uint32_t)cur_off);
        p32(toc, (uint32_t)bytes.size());

        size_t needed_pad = cur_off - (4 + N*24 + data_blob.size());
        if (needed_pad > 0) data_blob.insert(data_blob.end(), needed_pad, 0);

        data_blob.insert(data_blob.end(), bytes.begin(), bytes.end());
        cur_off = AlignUp(cur_off + bytes.size(), (size_t)align);
    }

    std::vector<uint8_t> out;
    p32(out, (uint32_t)N);
    out.insert(out.end(), toc.begin(), toc.end());
    out.insert(out.end(), data_blob.begin(), data_blob.end());
    return out;
}
