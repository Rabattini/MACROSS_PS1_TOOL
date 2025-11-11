#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <array>
struct GkoEntry {
    std::string name;
    uint32_t offset;
    uint32_t size;
    std::vector<uint8_t> data;
    std::array<uint8_t,16> name_raw;
};

std::vector<GkoEntry> ParseGKO(const std::vector<uint8_t>& bytes);
int DetectGKOAlignment(const std::vector<GkoEntry>& entries);
std::vector<uint8_t> BuildGKO_PreserveOrder(const std::vector<GkoEntry>& orderEntries,
                                            const std::filesystem::path& folder);
