// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        wprintf(L"uso: taskbar-styler <comando>\n");
        wprintf(L"comandos: load, status, unload\n");
        return 2;
    }
    wprintf(L"comando desconhecido: %s\n", argv[1]);
    return 2;
}
