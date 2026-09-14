// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <cli/loader.h>
#include <styler/config.h>
#include <styler/selector.h>
#include <tap/ipc.h>

namespace {

// Mirrors the gate in src/tap/change_subscription.cpp: the standing
// subscription refuses to start unless this is 1 (spike-standing-crash.md,
// vendor:10904-10914 - composition diagnostics corrupt the heap). Read-only
// here; `setup` is the only thing that writes it, and only with consent and
// elevation.
constexpr wchar_t kCompDiagKey[] = L"Software\\Microsoft\\XAML\\Debug";
constexpr wchar_t kCompDiagValue[] = L"DisableCompositionDiag";

bool CompositionDiagDisabled() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    LONG st = RegGetValueW(HKEY_LOCAL_MACHINE, kCompDiagKey, kCompDiagValue,
                           RRF_RT_REG_DWORD, nullptr, &value, &size);
    return st == ERROR_SUCCESS && value == 1;
}

int Usage() {
    wprintf(L"uso: taskbar-styler <comando>\n\n");
    wprintf(L"  load             carrega o TAP no explorer.exe\n");
    wprintf(L"  apply <ThemeId>  aplica um tema (ao vivo, se o TAP ja estiver carregado)\n");
    wprintf(L"  reset            desfaz o tema aplicado (ao vivo)\n");
    wprintf(L"  list             lista os temas disponiveis\n");
    wprintf(L"  status           mostra o estado\n");
    wprintf(L"  setup            grava DisableCompositionDiag=1 (precisa de administrador)\n");
    wprintf(L"  unload           explica por que nao ha descarregamento\n");
    return 2;
}

int CmdLoad();  // Forward: CmdApply falls back to it when no TAP is loaded.

int CmdLoad() {
    DWORD pid = styler::cli::FindTaskbarPid();
    if (pid == 0) {
        wprintf(L"erro: janela Shell_TrayWnd nao encontrada. "
                L"O explorer.exe esta rodando?\n");
        return 1;
    }

    std::wstring tap = styler::cli::TapDllPath();
    if (tap.empty()) {
        wprintf(L"erro: TaskbarStyler.Tap.dll nao encontrada ao lado do "
                L"executavel.\n");
        return 1;
    }

    wprintf(L"explorer.exe pid=%lu\n", pid);
    wprintf(L"tap: %s\n", tap.c_str());

    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co)) {
        wprintf(L"erro: CoInitializeEx %s\n",
                styler::cli::DescribeHresult(co).c_str());
        return 1;
    }

    std::wstring themes = styler::cli::ThemesDir();
    if (themes.empty()) {
        wprintf(L"aviso: pasta themes nao encontrada ao lado do executavel; "
                L"o TAP carrega, mas nao tera temas para aplicar.\n");
    }
    auto r = styler::cli::LoadTap(pid, tap, themes);

    if (FAILED(r.hr)) {
        // Deliberately no retry loop: a process that keeps trying to load code
        // into another one is indistinguishable from an attack, to the user and
        // to any EDR watching. Spec section 6.5.
        wprintf(L"falha ao carregar: %s\n",
                styler::cli::DescribeHresult(r.hr).c_str());
        return 1;
    }

    wprintf(L"carregado via %s\n", r.connection.c_str());
    if (!CompositionDiagDisabled()) {
        wprintf(L"aviso: DisableCompositionDiag nao esta em 1; o TAP vai "
                L"exportar a arvore, mas nao vai assinar mudancas (rode "
                L"\"taskbar-styler setup\" como administrador).\n");
    }
    wprintf(L"log em %%LOCALAPPDATA%%\\TaskbarStyler\\log.txt\n");
    return 0;
}

styler::Config ReadConfig() {
    styler::Config c;
    std::wstring path = styler::tap::ConfigPath();
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) {
        return c;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    try {
        c = styler::ParseConfigJson(buf.str());
    } catch (const styler::ParseError& ex) {
        wprintf(L"aviso: config.json invalido (%S); sera reescrito\n", ex.what());
    }
    return c;
}

bool WriteConfig(const styler::Config& c) {
    std::filesystem::path path(styler::tap::ConfigPath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        wprintf(L"erro: nao consegui escrever %s\n", path.c_str());
        return false;
    }
    out << styler::SerializeConfigJson(c);
    return true;
}

// True when a TAP is resident and waiting: it is the one that creates the
// event. Signals it when so.
bool SignalReloadIfLoaded() {
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, styler::tap::kReloadEventName);
    if (!ev) {
        return false;
    }
    SetEvent(ev);
    CloseHandle(ev);
    return true;
}

bool ThemeExists(const std::wstring& id) {
    std::wstring dir = styler::cli::ThemesDir();
    if (dir.empty() || id.empty()) {
        return false;
    }
    return std::filesystem::exists(std::filesystem::path(dir) / (id + L".json"));
}

int CmdApply(const wchar_t* id) {
    if (!id || !*id) {
        wprintf(L"uso: taskbar-styler apply <ThemeId>   (veja: list)\n");
        return 2;
    }
    if (!ThemeExists(id)) {
        wprintf(L"erro: tema '%s' nao encontrado em %s\n", id,
                styler::cli::ThemesDir().c_str());
        return 1;
    }
    styler::Config c = ReadConfig();
    c.theme = id;
    if (!WriteConfig(c)) {
        return 1;
    }
    if (SignalReloadIfLoaded()) {
        wprintf(L"tema '%s' enviado ao TAP ja carregado\n", id);
        return 0;
    }
    wprintf(L"TAP nao carregado; carregando com o tema '%s'\n", id);
    return CmdLoad();  // SetSite reads the config it just wrote.
}

int CmdReset() {
    styler::Config c = ReadConfig();
    c.theme.clear();
    if (!WriteConfig(c)) {
        return 1;
    }
    if (SignalReloadIfLoaded()) {
        wprintf(L"tema desfeito\n");
    } else {
        wprintf(L"nenhum TAP carregado; config limpo\n");
    }
    return 0;
}

int CmdList() {
    std::wstring dir = styler::cli::ThemesDir();
    if (dir.empty()) {
        wprintf(L"erro: pasta themes nao encontrada ao lado do executavel\n");
        return 1;
    }
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != L".json" ||
            entry.path().filename() == L"credits.json") {
            continue;
        }
        wprintf(L"%s\n", entry.path().stem().c_str());
        ++count;
    }
    wprintf(L"\n%d temas\n", count);
    return 0;
}

int CmdStatus() {
    DWORD pid = styler::cli::FindTaskbarPid();
    if (pid == 0) {
        wprintf(L"explorer.exe: nao encontrado\n");
        return 1;
    }
    wprintf(L"explorer.exe pid=%lu\n", pid);

    // The TAP writes its log from inside explorer; that file is the only status
    // channel this plan has. The tray of Plano 4 replaces it with a real one.
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring log = std::wstring(base) + L"\\TaskbarStyler\\log.txt";
        if (GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES) {
            wprintf(L"log: %s\n", log.c_str());
        } else {
            wprintf(L"log: ainda nao existe (o TAP nunca carregou)\n");
        }
    }
    wprintf(CompositionDiagDisabled()
                ? L"composition diagnostics: desativadas "
                  L"(DisableCompositionDiag=1) - a assinatura permanente pode "
                  L"iniciar\n"
                : L"composition diagnostics: ativadas - a assinatura "
                  L"permanente nao inicia; rode \"taskbar-styler setup\" como "
                  L"administrador\n");

    styler::Config c = ReadConfig();
    wprintf(L"tema configurado: %s\n", c.theme.empty() ? L"nenhum" : c.theme.c_str());

    HANDLE reload_event = OpenEventW(SYNCHRONIZE, FALSE, styler::tap::kReloadEventName);
    bool tap_loaded = reload_event != nullptr;
    if (reload_event) {
        CloseHandle(reload_event);
    }
    wprintf(L"TAP: %s\n", tap_loaded ? L"carregado" : L"nao carregado");
    return 0;
}

int CmdSetup() {
    wprintf(L"Isto grava HKLM\\%s\\%s = 1.\n\n", kCompDiagKey, kCompDiagValue);
    wprintf(L"E o valor que o Windows.UI.Xaml.dll le uma unica vez, de dentro "
            L"do proprio\nAdviseVisualTreeChange, para decidir se cria as "
            L"diagnostics de composition.\nSem ele, um "
            L"Windows.UI.Composition.SpriteVisual adicionado por qualquer\n"
            L"thread de UI do explorer.exe (o Task View, por exemplo) pode "
            L"corromper o\nheap enquanto a assinatura permanente do TAP esta "
            L"ativa (vendor:10904-10914).\n\n"
            L"E estado global da maquina - afeta as diagnostics XAML de "
            L"qualquer processo,\nnao so o explorer.exe. Para desfazer, "
            L"elevado:\n"
            L"  reg delete \"HKLM\\%s\" /v %s /f\n\n",
            kCompDiagKey, kCompDiagValue);

    std::wstring params = std::wstring(L"add \"HKLM\\") + kCompDiagKey +
                          L"\" /v " + kCompDiagValue +
                          L" /t REG_DWORD /d 1 /f";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"reg.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        wprintf(L"erro: nao foi possivel elevar (UAC recusado?): %lu\n",
                GetLastError());
        return 1;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);
    if (exit_code != 0) {
        wprintf(L"reg.exe saiu com codigo %lu\n", exit_code);
        return 1;
    }
    wprintf(L"%s=1 gravado.\n", kCompDiagValue);
    return 0;
}

int CmdUnload() {
    wprintf(L"O TAP nao se descarrega. Arrancar uma DLL COM de um processo\n"
            L"vivo, com callbacks do XAML possivelmente em voo, e fragil.\n"
            L"Ele fica residente e inerte ate o proximo reinicio do explorer.\n\n"
            L"Para limpar agora, reinicie o Explorador do Windows pelo\n"
            L"Gerenciador de Tarefas.\n");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return Usage();
    }
    if (wcscmp(argv[1], L"load") == 0) {
        return CmdLoad();
    }
    if (wcscmp(argv[1], L"apply") == 0) {
        return CmdApply(argc > 2 ? argv[2] : nullptr);
    }
    if (wcscmp(argv[1], L"reset") == 0) {
        return CmdReset();
    }
    if (wcscmp(argv[1], L"list") == 0) {
        return CmdList();
    }
    if (wcscmp(argv[1], L"status") == 0) {
        return CmdStatus();
    }
    if (wcscmp(argv[1], L"setup") == 0) {
        return CmdSetup();
    }
    if (wcscmp(argv[1], L"unload") == 0) {
        return CmdUnload();
    }
    wprintf(L"comando desconhecido: %s\n\n", argv[1]);
    return Usage();
}
