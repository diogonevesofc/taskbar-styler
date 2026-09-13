// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <cli/loader.h>

namespace {

int Usage() {
    wprintf(L"uso: taskbar-styler <comando>\n\n");
    wprintf(L"  load     carrega o TAP no explorer.exe\n");
    wprintf(L"  status   mostra o estado\n");
    wprintf(L"  unload   explica por que nao ha descarregamento\n");
    return 2;
}

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
    wprintf(L"log em %%LOCALAPPDATA%%\\TaskbarStyler\\log.txt\n");
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
    if (wcscmp(argv[1], L"status") == 0) {
        return CmdStatus();
    }
    if (wcscmp(argv[1], L"unload") == 0) {
        return CmdUnload();
    }
    wprintf(L"comando desconhecido: %s\n\n", argv[1]);
    return Usage();
}
