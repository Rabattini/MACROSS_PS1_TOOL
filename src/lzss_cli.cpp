#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "lzss.h"

static void PrintUsage() {
    std::wcout << L"MACROSS LZSS CLI (PS1-compatible)\n"
               << L"Uso:\n"
               << L"  lzss_cli compress  <input> [-o <out>] [-p rapido|equilibrado|maximo] [--no-lazy]\n"
               << L"  lzss_cli decompress <input> [-o <out>] [--out-len <N>]\n\n"
               << L"Padrões:\n"
               << L"  -p equilibrado, lazy matching ativado\n"
               << L"  compress out  = <input>.lzss\n"
               << L"  decompress out = <input>.decomp.bin\n";
}

static bool ReadAll(const std::filesystem::path& p, std::vector<uint8_t>& buf) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    buf.resize(sz);
    f.read((char*)buf.data(), sz);
    return true;
}
static bool WriteAll(const std::filesystem::path& p, const std::vector<uint8_t>& buf) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write((const char*)buf.data(), buf.size());
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { PrintUsage(); return 1; }

    std::wstring cmd = argv[1];
    std::filesystem::path in = argv[2];
    std::filesystem::path out;
    bool lazy = true;
    int bucket_limit = 128;
    int max_candidates = 256;
    size_t out_len = 0; // only for decompress

    for (int i = 3; i < argc; ++i) {
        std::wstring a = argv[i];
        if ((a == L"-o" || a == L"--out") && i+1 < argc) {
            out = argv[++i];
        } else if (a == L"-p" && i+1 < argc) {
            std::wstring prof = argv[++i];
            if (prof == L"rapido" || prof == L"rápido") { bucket_limit = 64;  max_candidates = 128; }
            else if (prof == L"maximo" || prof == L"máximo" || prof == L"maxima" || prof == L"máxima") { bucket_limit = 256; max_candidates = 1024; }
            else { bucket_limit = 128; max_candidates = 256; } // equilibrado
        } else if (a == L"--no-lazy") {
            lazy = false;
        } else if (a == L"--out-len" && i+1 < argc) {
            out_len = (size_t)_wtoi(argv[++i]);
        } else if (a == L"-h" || a == L"--help" || a == L"/?") {
            PrintUsage();
            return 0;
        }
    }

    std::vector<uint8_t> input;
    if (!ReadAll(in, input)) {
        std::wcerr << L"Erro ao ler arquivo de entrada: " << in << L"\n";
        return 2;
    }

    if (cmd == L"compress") {
        if (out.empty()) out = in.wstring() + L".lzss";
        auto comp = CompressLZSS_PSX(input, bucket_limit, max_candidates, lazy);
        if (!WriteAll(out, comp)) {
            std::wcerr << L"Erro ao salvar: " << out << L"\n"; return 3;
        }
        std::wcout << L"OK: " << in << L" -> " << out << L"  [" << comp.size() << L" bytes]\n";
        return 0;
    } else if (cmd == L"decompress") {
        if (out.empty()) out = in.wstring() + L".decomp.bin";
        auto decomp = DecompressLZSS_PSX(input, out_len);
        if (!WriteAll(out, decomp)) {
            std::wcerr << L"Erro ao salvar: " << out << L"\n"; return 3;
        }
        std::wcout << L"OK: " << in << L" -> " << out << L"  [" << decomp.size() << L" bytes]\n";
        return 0;
    } else {
        PrintUsage();
        return 1;
    }
}
