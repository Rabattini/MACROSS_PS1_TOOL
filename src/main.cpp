#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <ShObjIdl.h>   // IFileDialog
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <algorithm>
#include <iomanip>      // setw, setprecision

// Inclua seus headers locais
#include "lzss.h"
#include "gko.h"
#include "pud.h"
#include "../res/resource.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Ole32.lib") // CoInitializeEx / IFileDialog

// App window + controls
#define IDC_TAB               100
// GKO controls
#define IDC_GKO_OPEN          101
#define IDC_GKO_UNPACK        102
#define IDC_GKO_PACK          103
#define IDC_GKO_INFO          104
// PUD controls
#define IDC_PUD_OPEN          201
#define IDC_PUD_EX_COMP       202
#define IDC_PUD_EX_DECOMP     203
#define IDC_PUD_PROFILE       204
#define IDC_PUD_LAZY          205
#define IDC_PUD_PACK_RAW      206
#define IDC_PUD_PACK_COMP     207
#define IDC_PUD_INFO          208
#define IDC_PUD_PROFILE_LABEL 209
#define IDC_PUD_GROUP_EX      210
#define IDC_PUD_GROUP_PACK    211
// Log
#define IDC_LOG               300

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND hTab = nullptr;
HWND hLog = nullptr;

// GKO UI controls
HWND hGkoOpen = nullptr, hGkoUnpack = nullptr, hGkoPack = nullptr, hGkoInfo = nullptr;
// PUD UI controls
HWND hPudOpen = nullptr, hPudExComp = nullptr, hPudExDecomp = nullptr,
hPudProfile = nullptr, hPudLazy = nullptr, hPudPackRaw = nullptr, hPudPackComp = nullptr, hPudInfo = nullptr,
hPudProfileLabel = nullptr, hPudGroupExtract = nullptr, hPudGroupPack = nullptr;

// Data state
std::vector<GkoEntry> g_gkoEntries;
int g_gkoAlign = 1;
std::vector<uint8_t> g_pudBytes;
PudFile g_pud;
bool g_hasPud = false;

// Fonts
HFONT g_hFontMono = nullptr;

// ===== Helpers =====
static std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Falha ao abrir: " + p.string());
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(sz);
    f.read((char*)buf.data(), sz);
    return buf;
}
static void WriteAllBytes(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Falha ao salvar: " + p.string());
    f.write((const char*)bytes.data(), bytes.size());
}

static void Log(const std::wstring& s) {
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}
static void LogLn(const std::wstring& s) { Log(s + L"\r\n"); }

static void SetInfo(HWND hEdit, const std::wstring& s) { SetWindowTextW(hEdit, s.c_str()); }

// Tamanho “humano”
static std::wstring HumanSize(uint64_t b) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int u = 0;
    double v = (double)b;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::wstringstream ss;
    ss.setf(std::ios::fixed);
    ss << std::setprecision(u == 0 ? 0 : 1) << v << L' ' << units[u];
    return ss.str();
}

// ===== Folder picker moderno (estilo OpenFileDialog) =====
static std::wstring PickFolderModern(HWND owner, const wchar_t* title) {
    std::wstring result;
    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) return L"";
    DWORD opts = 0;
    if (SUCCEEDED(pfd->GetOptions(&opts))) {
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    }
    if (title && *title) pfd->SetTitle(title);

    hr = pfd->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { pfd->Release(); return L""; }
    if (FAILED(hr)) { pfd->Release(); return L""; }

    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    if (SUCCEEDED(hr) && psi) {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
            result = pszPath;
            CoTaskMemFree(pszPath);
        }
        psi->Release();
    }
    pfd->Release();
    return result;
}

static std::wstring OpenFileDlg(HWND owner, const wchar_t* filter, const wchar_t* defExt = nullptr) {
    OPENFILENAMEW ofn{};
    wchar_t file[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = defExt;
    if (GetOpenFileNameW(&ofn))
        return ofn.lpstrFile;
    return L"";
}
static std::wstring SaveFileDlg(HWND owner, const wchar_t* filter, const wchar_t* defExt) {
    OPENFILENAMEW ofn{};
    wchar_t file[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = defExt;
    if (GetSaveFileNameW(&ofn))
        return ofn.lpstrFile;
    return L"";
}

// ===== Layout =====
static void DoLayout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int logHeight = 180;
    int margin = 8;

    // Tab at top
    MoveWindow(hTab, margin, margin, w - 2 * margin, h - logHeight - 3 * margin, TRUE);
    // Log at bottom
    MoveWindow(hLog, margin, h - logHeight - margin, w - 2 * margin, logHeight, TRUE);

    // === área interior "página" do Tab, em coordenadas do PAI ===
    RECT page; GetClientRect(hTab, &page);
    TabCtrl_AdjustRect(hTab, FALSE, &page);
    // converter do espaço do TAB para o espaço do PAI (janela principal)
    MapWindowPoints(hTab, hwnd, reinterpret_cast<LPPOINT>(&page), 2);

    int tx = page.left + margin;
    int ty = page.top + margin;
    int tw = (page.right - page.left) - 2 * margin;
    int th = (page.bottom - page.top) - 2 * margin;

    // ----- GKO -----
    const int rowH = 28;
    const int rowGap = 12;

    MoveWindow(hGkoOpen, tx, ty, 220, rowH, TRUE);
    MoveWindow(hGkoUnpack, tx + 230, ty, 260, rowH, TRUE);
    MoveWindow(hGkoPack, tx + 500, ty, 260, rowH, TRUE);

    int infoY = ty + rowH + rowGap;
    int infoH = (page.bottom - margin) - infoY;              // até perto do log
    infoH = std::max(80, infoH);                             // mínimo
    MoveWindow(hGkoInfo, tx, infoY, tw, infoH, TRUE);

    // ----- PUD (com divisórias) -----
    int py = ty;

    // Grupo: Extrair / Descomprimir
    int groupHdr = 18;
    int exInnerTop = py + groupHdr + 6;
    int exHeight = 80;
    MoveWindow(hPudGroupExtract, tx, py, tw, exHeight, TRUE);

    MoveWindow(hPudOpen, tx + 12, exInnerTop, 220, rowH, TRUE);
    MoveWindow(hPudExComp, tx + 12 + 230, exInnerTop, 280, rowH, TRUE);
    MoveWindow(hPudExDecomp, tx + 12 + 230 + 290, exInnerTop, 320, rowH, TRUE);

    py += exHeight + 12;

    // Grupo: Pack / Comprimir (altura dinâmica para aproveitar o espaço restante)
    int packInnerTop = py + groupHdr + 6;
    int packHeight = (page.bottom - margin) - py;            // usa tudo até o rodapé da página
    packHeight = std::max(160, packHeight);
    MoveWindow(hPudGroupPack, tx, py, tw, packHeight, TRUE);

    MoveWindow(hPudProfileLabel, tx + 12, packInnerTop, 160, 24, TRUE);
    MoveWindow(hPudProfile, tx + 12 + 170, packInnerTop - 2, 220, 200, TRUE);
    MoveWindow(hPudLazy, tx + 12 + 400, packInnerTop - 2, 320, 28, TRUE);

    MoveWindow(hPudPackRaw, tx + 12, packInnerTop + 40, 280, rowH, TRUE);
    MoveWindow(hPudPackComp, tx + 12 + 290, packInnerTop + 40, 260, rowH, TRUE);

    // Info do PUD ocupa o restante dentro do group
    int pudInfoY = packInnerTop + 74;
    int pudInfoH = (py + packHeight - 12) - pudInfoY;
    pudInfoH = std::max(80, pudInfoH);
    MoveWindow(hPudInfo, tx + 12, pudInfoY, tw - 24, pudInfoH, TRUE);
}

static void ShowTab(int idx) {
    // idx: 0 = GKO, 1 = PUD
    BOOL gkoVisible = (idx == 0);
    BOOL pudVisible = (idx == 1);
    ShowWindow(hGkoOpen, gkoVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hGkoUnpack, gkoVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hGkoPack, gkoVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hGkoInfo, gkoVisible ? SW_SHOW : SW_HIDE);

    ShowWindow(hPudGroupExtract, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudGroupPack, pudVisible ? SW_SHOW : SW_HIDE);

    ShowWindow(hPudOpen, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudExComp, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudExDecomp, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudProfileLabel, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudProfile, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudLazy, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudPackRaw, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudPackComp, pudVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(hPudInfo, pudVisible ? SW_SHOW : SW_HIDE);
}

// ===== Handlers GKO =====
static void OnOpenGKO() {
    auto p = OpenFileDlg(g_hWnd, L"GKO Files\0*.gko;*.GKO\0All Files\0*.*\0\0");
    if (p.empty()) return;
    try {
        auto bytes = ReadAllBytes(p);
        g_gkoEntries = ParseGKO(bytes);
        g_gkoAlign = DetectGKOAlignment(g_gkoEntries);

        std::wstringstream ss;
        auto fn = std::filesystem::path(p).filename().wstring();
        ss << L"GKO: " << fn
            << L"\r\nEntradas: " << g_gkoEntries.size()
            << L"\r\nAlinhamento detectado: 0x" << std::hex << g_gkoAlign << std::dec
            << L"\r\n\r\n";

        ss << L"Idx  Tam(bytes)  Tam(hum)   Nome\r\n";
        ss << L"---- ----------  ---------  -----------------------------------------------\r\n";

        for (size_t i = 0; i < g_gkoEntries.size(); ++i) {
            const auto& e = g_gkoEntries[i];
            const size_t sz = e.data.size();
            std::wstring wname = std::filesystem::path(std::wstring(e.name.begin(), e.name.end())).filename().wstring();

            ss << std::setw(4) << i << L"  "
                << std::setw(10) << (unsigned long long)sz << L"  "
                << std::setw(9) << HumanSize(sz) << L"  "
                << wname << L"\r\n";
        }

        SetInfo(hGkoInfo, ss.str());
        LogLn(L"[GKO] Arquivo carregado e listado.");
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void OnUnpackGKO() {
    if (g_gkoEntries.empty()) { MessageBoxW(g_hWnd, L"Abra um arquivo .GKO primeiro.", L"Informação", MB_ICONINFORMATION); return; }
    auto folder = PickFolderModern(g_hWnd, L"Escolha a pasta de destino para extração");
    if (folder.empty()) return;
    try {
        for (auto& e : g_gkoEntries) {
            std::filesystem::path out = std::filesystem::path(folder) / std::filesystem::path(e.name).filename();
            WriteAllBytes(out, e.data);
        }
        std::wstringstream ss; ss << L"Extração concluída!\r\n\r\n" << g_gkoEntries.size() << L" arquivos extraídos para:\r\n" << folder;
        LogLn(L"[GKO] Extração concluída.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void OnPackGKO() {
    auto folder = PickFolderModern(g_hWnd, L"Escolha a pasta com os arquivos para reempacotar");
    if (folder.empty()) return;
    auto order_entries = g_gkoEntries;
    if (order_entries.empty()) {
        auto tpl = OpenFileDlg(g_hWnd, L"GKO Files\0*.gko;*.GKO\0All Files\0*.*\0\0");
        if (tpl.empty()) {
            MessageBoxW(g_hWnd, L"É necessário selecionar um arquivo .GKO original para preservar a ordem.", L"Erro", MB_ICONERROR);
            return;
        }
        try {
            auto b = ReadAllBytes(tpl);
            order_entries = ParseGKO(b);
            std::wstringstream ss; ss << L"Usando ordem do arquivo: " << std::filesystem::path(tpl).filename().c_str();
            LogLn(ss.str());
        }
        catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); return; }
    }
    try {
        auto gko_bytes = BuildGKO_PreserveOrder(order_entries, folder);
        auto out = SaveFileDlg(g_hWnd, L"GKO Files\0*.gko\0All Files\0*.*\0\0", L"gko");
        if (out.empty()) return;
        WriteAllBytes(out, gko_bytes);
        std::wstringstream ss; ss << L"Arquivo GKO criado com sucesso!\r\n\r\nArquivo: "
            << std::filesystem::path(out).filename().c_str()
            << L"\r\nItens: " << order_entries.size();
        LogLn(L"[GKO] Empacotamento concluído.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

// ===== Handlers PUD =====
static void OnOpenPUD() {
    auto p = OpenFileDlg(g_hWnd, L"PUD Files\0*.pud;*.PUD\0All Files\0*.*\0\0");
    if (p.empty()) return;
    try {
        g_pudBytes = ReadAllBytes(p);
        g_pud = ParsePUD(g_pudBytes, std::filesystem::path(p).filename().string());
        g_hasPud = true;
        std::wstringstream ss;
        ss << L"PUD aberto com sucesso: " << std::filesystem::path(p).filename().c_str()
            << L"\r\n\r\nNúmero de blocos: " << g_pud.blocks.size();
        SetInfo(hPudInfo, ss.str());
        LogLn(L"[PUD] Arquivo carregado.");
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void OnExtractPUD_Compressed() {
    if (!g_hasPud) { MessageBoxW(g_hWnd, L"Abra um arquivo .PUD primeiro.", L"Informação", MB_ICONINFORMATION); return; }
    auto outdir = PickFolderModern(g_hWnd, L"Escolha a pasta de destino para blocos comprimidos");
    if (outdir.empty()) return;
    try {
        auto stem = std::filesystem::path(g_pud.path).stem().wstring();
        for (auto& b : g_pud.blocks) {
            std::vector<uint8_t> comp(g_pudBytes.begin() + b.data_off, g_pudBytes.begin() + b.data_end);
            std::filesystem::path out = std::filesystem::path(outdir) / (stem + L".block" + std::to_wstring(b.idx) + L".bin");
            WriteAllBytes(out, comp);
        }
        std::wstringstream ss; ss << L"Extração de blocos comprimidos concluída!\r\n\r\n"
            << g_pud.blocks.size() << L" blocos extraídos para:\r\n" << outdir;
        LogLn(L"[PUD] Extração (comprimidos) concluída.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void OnExtractPUD_Decompressed() {
    if (!g_hasPud) { MessageBoxW(g_hWnd, L"Abra um arquivo .PUD primeiro.", L"Informação", MB_ICONINFORMATION); return; }
    auto outdir = PickFolderModern(g_hWnd, L"Escolha a pasta de destino para blocos descomprimidos");
    if (outdir.empty()) return;
    try {
        auto stem = std::filesystem::path(g_pud.path).stem().wstring();
        for (auto& b : g_pud.blocks) {
            std::vector<uint8_t> comp(g_pudBytes.begin() + b.data_off, g_pudBytes.begin() + b.data_end);
            auto raw = DecompressLZSS_PSX(comp, b.dsize);
            if (raw.size() != b.dsize) {
                std::wstringstream warn; warn << L"Aviso: bloco " << b.idx
                    << L" descomprimido com tamanho " << raw.size() << L" != dsize " << b.dsize;
                LogLn(warn.str());
            }
            std::filesystem::path out = std::filesystem::path(outdir) / (stem + L".block" + std::to_wstring(b.idx) + L".decomp.bin");
            WriteAllBytes(out, raw);
        }
        std::wstringstream ss; ss << L"Extração de blocos descomprimidos concluída!\r\n\r\n"
            << g_pud.blocks.size() << L" blocos extraídos para:\r\n" << outdir;
        LogLn(L"[PUD] Extração (descomprimidos) concluída.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void GetCompressionParams(int& bucket_limit, int& max_candidates, bool& lazy) {
    int idx = (int)SendMessageW(hPudProfile, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) idx = 1; // default Equilibrado
    lazy = (SendMessageW(hPudLazy, BM_GETCHECK, 0, 0) == BST_CHECKED);
    switch (idx) {
    case 0: bucket_limit = 64;  max_candidates = 128;  break;      // Rápido
    case 2: bucket_limit = 256; max_candidates = 1024; break;      // Máxima compressão
    default: bucket_limit = 128; max_candidates = 256; break;      // Equilibrado
    }
}

static bool CollectBlockFiles(std::vector<std::vector<uint8_t>>& outBlocks,
    const std::filesystem::path& folder,
    const std::wstring& stem,
    bool use_raw) {
    if (!g_hasPud) return false;
    outBlocks.clear();
    try {
        for (auto& blk : g_pud.blocks) {
            std::wstring file1, file2;
            if (use_raw) {
                file1 = stem + L".block" + std::to_wstring(blk.idx) + L".decomp.bin";
                file2 = L"block" + std::to_wstring(blk.idx) + L".decomp.bin";
            }
            else {
                file1 = stem + L".block" + std::to_wstring(blk.idx) + L".bin";
                file2 = L"block" + std::to_wstring(blk.idx) + L".bin";
            }
            std::filesystem::path p1 = folder / file1;
            std::filesystem::path p2 = folder / file2;
            if (std::filesystem::exists(p1)) outBlocks.push_back(ReadAllBytes(p1));
            else if (std::filesystem::exists(p2)) outBlocks.push_back(ReadAllBytes(p2));
            else {
                std::wstringstream ss;
                ss << L"Não foi encontrado arquivo para bloco " << blk.idx << L": esperado " << file1;
                MessageBoxW(g_hWnd, ss.str().c_str(), L"Erro", MB_ICONERROR);
                return false;
            }
        }
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); return false; }
    return true;
}

static void OnPackPUD_FromRaw() {
    if (!g_hasPud) { MessageBoxW(g_hWnd, L"Abra um arquivo .PUD (template) primeiro.", L"Informação", MB_ICONINFORMATION); return; }
    auto folder = PickFolderModern(g_hWnd, L"Escolha a pasta com blocos descomprimidos (.decomp.bin)");
    if (folder.empty()) return;
    std::vector<std::vector<uint8_t>> blocks;
    auto stem = std::filesystem::path(g_pud.path).stem().wstring();
    if (!CollectBlockFiles(blocks, folder, stem, true)) return;
    int bl = 128, mc = 256; bool lazy = true;
    GetCompressionParams(bl, mc, lazy);
    try {
        auto new_pud = BuildPUD_FromBlocks(g_pud, blocks, true, bl, mc, lazy);
        auto out = SaveFileDlg(g_hWnd, L"PUD Files\0*.pud\0All Files\0*.*\0\0", L"pud");
        if (out.empty()) return;
        WriteAllBytes(out, new_pud);
        std::wstringstream ss;
        ss << L"Arquivo PUD criado com sucesso!\r\n\r\nArquivo: "
            << std::filesystem::path(out).filename().c_str()
            << L"\r\nMétodo: Compressão de blocos descomprimidos";
        LogLn(L"[PUD] Empacotamento (a partir de RAW) concluído.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

static void OnPackPUD_FromComp() {
    if (!g_hasPud) { MessageBoxW(g_hWnd, L"Abra um arquivo .PUD (template) primeiro.", L"Informação", MB_ICONINFORMATION); return; }
    auto folder = PickFolderModern(g_hWnd, L"Escolha a pasta com blocos comprimidos (.bin)");
    if (folder.empty()) return;
    std::vector<std::vector<uint8_t>> blocks;
    auto stem = std::filesystem::path(g_pud.path).stem().wstring();
    if (!CollectBlockFiles(blocks, folder, stem, false)) return;
    try {
        auto new_pud = BuildPUD_FromBlocks(g_pud, blocks, false);
        auto out = SaveFileDlg(g_hWnd, L"PUD Files\0*.pud\0All Files\0*.*\0\0", L"pud");
        if (out.empty()) return;
        WriteAllBytes(out, new_pud);
        std::wstringstream ss;
        ss << L"Arquivo PUD criado com sucesso!\r\n\r\nArquivo: "
            << std::filesystem::path(out).filename().c_str()
            << L"\r\nMétodo: Blocos pré-comprimidos";
        LogLn(L"[PUD] Empacotamento (a partir de COMP) concluído.");
        MessageBoxW(g_hWnd, ss.str().c_str(), L"Sucesso", MB_ICONINFORMATION);
    }
    catch (const std::exception& e) { MessageBoxA(g_hWnd, e.what(), "Erro", MB_ICONERROR); }
}

// ===== Window procedure =====
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);

        // TabControl com WS_CLIPSIBLINGS (para não cobrir os controles)
        hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_TAB, g_hInst, nullptr);

        TCITEMW tie{}; tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)L"  GKO - Extração e Empacotamento  ";
        TabCtrl_InsertItem(hTab, 0, &tie);
        tie.pszText = (LPWSTR)L"  PUD - Extração, Compressão e Empacotamento  ";
        TabCtrl_InsertItem(hTab, 1, &tie);

        // GKO controls
        hGkoOpen = CreateWindowExW(0, L"BUTTON", L"Abrir arquivo .GKO", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_GKO_OPEN, g_hInst, nullptr);
        hGkoUnpack = CreateWindowExW(0, L"BUTTON", L"Extrair Conteúdo (Unpack)", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_GKO_UNPACK, g_hInst, nullptr);
        hGkoPack = CreateWindowExW(0, L"BUTTON", L"Reempacotar Pasta (Pack)", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_GKO_PACK, g_hInst, nullptr);
        hGkoInfo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_GKO_INFO, g_hInst, nullptr);

        // ==== PUD controls ====
        // Group boxes (divisórias)
        hPudGroupExtract = CreateWindowExW(0, L"BUTTON", L"Extrair / Descomprimir",
            WS_CHILD | BS_GROUPBOX,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_GROUP_EX, g_hInst, nullptr);

        hPudGroupPack = CreateWindowExW(0, L"BUTTON", L"Pack / Comprimir",
            WS_CHILD | BS_GROUPBOX,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_GROUP_PACK, g_hInst, nullptr);

        // Botões/controles dentro das divisórias
        hPudOpen = CreateWindowExW(0, L"BUTTON", L"Abrir arquivo .PUD", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_OPEN, g_hInst, nullptr);
        hPudExComp = CreateWindowExW(0, L"BUTTON", L"Extrair Comprimidos (.bin)", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_EX_COMP, g_hInst, nullptr);
        hPudExDecomp = CreateWindowExW(0, L"BUTTON", L"Extrair Descomprimidos (.decomp.bin)", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_EX_DECOMP, g_hInst, nullptr);

        // ComboBox CBS_DROPDOWNLIST + WS_VISIBLE
        hPudProfile = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_PROFILE, g_hInst, nullptr);

        SendMessageW(hPudProfile, CB_RESETCONTENT, 0, 0);
        SendMessageW(hPudProfile, CB_ADDSTRING, 0, (LPARAM)L"Rápido");
        SendMessageW(hPudProfile, CB_ADDSTRING, 0, (LPARAM)L"Equilibrado");
        SendMessageW(hPudProfile, CB_ADDSTRING, 0, (LPARAM)L"Máxima compressão");
        SendMessageW(hPudProfile, CB_SETCURSEL, 1, 0);

        hPudLazy = CreateWindowExW(0, L"BUTTON", L"Ativar Lazy Matching (melhor compressão)",
            WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_LAZY, g_hInst, nullptr);
        SendMessageW(hPudLazy, BM_SETCHECK, BST_CHECKED, 0);

        hPudPackRaw = CreateWindowExW(0, L"BUTTON", L"Criar PUD de Descomprimidos", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_PACK_RAW, g_hInst, nullptr);
        hPudPackComp = CreateWindowExW(0, L"BUTTON", L"Criar PUD de Comprimidos", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_PACK_COMP, g_hInst, nullptr);
        hPudInfo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_INFO, g_hInst, nullptr);

        // Label
        hPudProfileLabel = CreateWindowExW(0, L"STATIC", L"Nível de compressão:", WS_CHILD,
            0, 0, 0, 0, hwnd, (HMENU)IDC_PUD_PROFILE_LABEL, g_hInst, nullptr);

        // Log
        hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, g_hInst, nullptr);

        // === Fonte GUI padrão ===
        {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            const HWND allCtrls[] = {
                hTab,
                hGkoOpen, hGkoUnpack, hGkoPack, hGkoInfo,
                hPudGroupExtract, hPudGroupPack,
                hPudOpen, hPudExComp, hPudExDecomp, hPudProfile,
                hPudLazy, hPudPackRaw, hPudPackComp, hPudInfo, hPudProfileLabel,
                hLog
            };
            for (HWND h : allCtrls) if (h) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        // === Monoespaçada para Info/Log ===
        g_hFontMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_DONTCARE, L"Consolas");
        if (g_hFontMono) {
            SendMessageW(hGkoInfo, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            SendMessageW(hPudInfo, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            SendMessageW(hLog, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
        }

        SendMessageW(hGkoInfo, EM_LIMITTEXT, 16 * 1024 * 1024, 0);
        SendMessageW(hPudInfo, EM_LIMITTEXT, 16 * 1024 * 1024, 0);
        SendMessageW(hLog, EM_LIMITTEXT, 16 * 1024 * 1024, 0);

        ShowTab(0);
        DoLayout(hwnd);

        LogLn(L"================================================================================");
        LogLn(L"GKO & PUD Manager - Ferramenta de Pack/Unpack com compressão LZSS");
        LogLn(L"Interface Win32 - C++");
        LogLn(L"================================================================================");
        return 0;
    }
    case WM_SIZE:
        DoLayout(hwnd);
        // Garante que os controles do tab ativo ficam visíveis e repintam
        ShowTab(TabCtrl_GetCurSel(hTab));
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->idFrom == IDC_TAB && hdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(hTab);
            ShowTab(sel);
            DoLayout(hwnd);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_GKO_OPEN:      OnOpenGKO(); break;
        case IDC_GKO_UNPACK:    OnUnpackGKO(); break;
        case IDC_GKO_PACK:      OnPackGKO(); break;
        case IDC_PUD_OPEN:      OnOpenPUD(); break;
        case IDC_PUD_EX_COMP:   OnExtractPUD_Compressed(); break;
        case IDC_PUD_EX_DECOMP: OnExtractPUD_Decompressed(); break;
        case IDC_PUD_PACK_RAW:  OnPackPUD_FromRaw(); break;
        case IDC_PUD_PACK_COMP: OnPackPUD_FromComp(); break;
        }
        return 0;
    }
    case WM_DESTROY:
        // (Opcional) DeleteObject(g_hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // COM para IFileDialog
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    g_hInst = hInstance;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacrossPs1GkoPudWndClass";
    RegisterClassExW(&wc);

    // Janela principal com WS_CLIPCHILDREN (evita overdraw sobre os filhos)
    g_hWnd = CreateWindowExW(0, wc.lpszClassName,
        L"MACROSS PS1 - GKO & PUD Manager (LZSS) - Made by Rabatini (Luke)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 780,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) { CoUninitialize(); return 0; }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
