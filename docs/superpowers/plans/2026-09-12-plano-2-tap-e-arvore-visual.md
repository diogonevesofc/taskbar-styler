# Plano 2 — O TAP e a árvore visual

> **Para trabalhadores agênticos:** SUB-SKILL OBRIGATÓRIA: use
> superpowers:subagent-driven-development (recomendado) ou
> superpowers:executing-plans para implementar tarefa a tarefa. Os passos usam
> checkbox (`- [ ]`) para acompanhamento.

**Goal:** Uma DLL COM que o Windows carrega dentro do `explorer.exe` pela API de
diagnóstico do XAML, mais um CLI que a dispara, capaz de percorrer a árvore
visual da taskbar e exportá-la no mesmo formato dos seletores dos temas.

**Architecture:** O CLI chama `InitializeXamlDiagnosticsEx` apontando para o PID
do explorer e para a nossa DLL; o Windows a carrega sozinho — sem injeção. Dentro
do processo, a DLL implementa `IObjectWithSite`, recebe um `IXamlDiagnostics`,
obtém `IVisualTreeService3` e se registra com `AdviseVisualTreeChange`. Nenhum
byte de código é modificado em processo algum.

**Tech Stack:** C++20 (MSVC), COM registration-free, Windows SDK 10.0.26100.0,
CMake + Ninja, doctest.

**Spec:** `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md` — §3
(viabilidade, já provada por spike), §4.1 (fluxo de partida), §6 (ciclo de vida),
§7 (erros e observabilidade), §7.5 (exportar árvore visual).

**Decisões herdadas:** `docs/superpowers/plano-1-decisoes.md`.

## Escopo

### Dentro

- DLL TAP carregada no `explorer.exe`, sem injeção e sem patch de código.
- CLI com `load`, `status` e `unload`.
- Ciclo de vida: inicialização por thread, enumeração dos hosts XAML, detecção
  de hosts novos por `SetWinEventHook`.
- Exportação da árvore visual em formato de seletor.
- Log em arquivo com níveis, barato quando desligado.

### Fora — deliberadamente

- **Aplicar estilo algum.** Nenhum `ValueRule` é resolvido, nenhuma propriedade
  é escrita. Isso é o Plano 3.
- Portanto **nenhuma adição a `styler_core`**: resolução de constantes,
  `@Light`/`@Dark` e o matcher de elementos só têm consumidor no Plano 3 e vão
  para lá. `styler_core` não é sequer ligado ao TAP neste plano.
- Bandeja, config em `%APPDATA%`, evento nomeado de recarga: Plano 4.

O plano termina com algo que se roda e se vê. Isso derruba todo o risco de COM e
de ciclo de vida antes de qualquer código de estilo existir, e entrega desde já a
ferramenta da §7.5 que permite consertar um tema sozinho quando o Windows
atualizar.

## Global Constraints

- **Licença GPL-3.0.** Todo `.h`/`.cpp`/`.py` novo leva
  `// SPDX-License-Identifier: GPL-3.0-or-later`. Não em `CMakeLists.txt` nem em
  `*.yml` (Ruling 3 do Plano 1).
- **Idioma:** identificadores e comentários de código em **inglês**;
  documentação em **português**.
- **C++20**, MSVC do Visual Studio 2026 Community, Windows SDK 10.0.26100.0.
- **x64 apenas.** O `explorer.exe` é x64; uma DLL x86 não carrega nele.
- **Nenhuma API de injeção, nenhum patch de código.** São proibidos:
  `VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread`,
  `SetThreadContext`, e qualquer biblioteca de hooking (MinHook, Detours).
  Detecção de janela nova usa `SetWinEventHook`, que é API documentada. Este é o
  argumento central do projeto — spec §3 e §6.2.
- **Zero acesso à rede em runtime.** Nem telemetria, nem verificação de
  atualização.
- **Nada de diálogo modal a partir do TAP.** Ele roda dentro do explorer; um
  `MessageBox` de lá trava o shell.
- **`styler_core` permanece livre de Windows.** Este plano não o modifica.

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `src/tap/CMakeLists.txt` | Alvos `taskbar_styler_tap` (DLL) e `..._tap_lib` (estática, para teste) |
| `src/tap/tap.def` | Exporta `DllGetClassObject`, `DllCanUnloadNow` |
| `src/tap/clsid.h` | O CLSID do TAP, compartilhado com o CLI |
| `src/tap/log.h/.cpp` | Log com níveis, `OutputDebugStringW` + arquivo rotativo |
| `src/tap/tap_boundary.cpp` | **Toda** entrada chamável pelo XAML, cada uma catch-all |
| `src/tap/visual_tree_watcher.h/.cpp` | `IVisualTreeServiceCallback2`, bookkeeping de handles |
| `src/tap/tree_export.h/.cpp` | Travessia e formatação da árvore |
| `src/tap/thread_init.h/.cpp` | Init por thread, enumeração de hosts, WinEvent hook |
| `src/cli/CMakeLists.txt` | Alvo `taskbar_styler_cli` |
| `src/cli/main.cpp` | Subcomandos |
| `src/cli/loader.h/.cpp` | Achar o PID da taskbar, chamar `InitializeXamlDiagnosticsEx` |
| `tests/tap/` | Testes das partes puras |
| `docs/smoke-test.md` | Checklist manual — spec §8.2 |

**A fronteira mora em um arquivo.** `tap_boundary.cpp` contém
`DllGetClassObject`, `DllCanUnloadNow`, `SetSite` e `GetSite`, e nada mais. Cada
uma tem `catch (...)` que nunca propaga e retorna `S_OK` mesmo em erro — devolver
erro faz o XAML parar de mandar eventos (spec §7.1; upstream faz o mesmo,
`vendor/upstream/windows-11-taskbar-styler.wh.cpp:10604`). Assim "a fronteira
está protegida?" se responde abrindo um arquivo.

---

### Task 1: Alvos de build, CLSID e log

Estabelece a DLL e o exe como alvos que compilam, e o log que todo o resto usa.
Nenhuma lógica de COM ainda.

**Files:**
- Create: `src/tap/CMakeLists.txt`, `src/tap/tap.def`, `src/tap/clsid.h`,
  `src/tap/log.h`, `src/tap/log.cpp`
- Create: `src/cli/CMakeLists.txt`, `src/cli/main.cpp`
- Create: `tests/tap/CMakeLists.txt`, `tests/tap/test_log.cpp`
- Modify: `CMakeLists.txt` (raiz)

**Interfaces:**
- Consumes: nada.
- Produces:
  - `styler::tap::CLSID_TaskbarStylerTap` em `src/tap/clsid.h`.
  - `enum class styler::tap::LogLevel { Error = 0, Info = 1, Debug = 2 }`.
  - `void SetLogLevel(LogLevel)`, `LogLevel GetLogLevel()`.
  - `bool LogEnabled(LogLevel)`.
  - `void LogLine(LogLevel, std::wstring_view)`.
  - `void LogLineFormatted(LogLevel, const wchar_t*, ...)`.
  - Macro `STYLER_LOG(level, ...)` que checa o nível **antes** de formatar.
  - Alvos CMake `taskbar_styler_tap`, `taskbar_styler_tap_lib`,
    `taskbar_styler_cli`.

- [ ] **Step 1: Escrever o teste falhando**

`tests/tap/test_log.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/log.h>

using styler::tap::LogEnabled;
using styler::tap::LogLevel;
using styler::tap::SetLogLevel;

TEST_CASE("log level gates as a threshold") {
    SetLogLevel(LogLevel::Error);
    CHECK(LogEnabled(LogLevel::Error));
    CHECK_FALSE(LogEnabled(LogLevel::Info));
    CHECK_FALSE(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Info);
    CHECK(LogEnabled(LogLevel::Error));
    CHECK(LogEnabled(LogLevel::Info));
    CHECK_FALSE(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Debug);
    CHECK(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Error);  // restore the default
}

TEST_CASE("the macro does not evaluate its arguments when gated") {
    SetLogLevel(LogLevel::Error);

    int calls = 0;
    auto expensive = [&calls]() -> const wchar_t* {
        calls++;
        return L"x";
    };

    STYLER_LOG(LogLevel::Debug, L"%s", expensive());
    CHECK(calls == 0);

    STYLER_LOG(LogLevel::Error, L"%s", expensive());
    CHECK(calls == 1);
}
```

- [ ] **Step 2: Rodar e ver falhar**

```
cmake -S . -B build -G Ninja
cmake --build build
```

Esperado: FALHA de compilação, `cannot open source file "tap/log.h"`.

- [ ] **Step 3: Escrever `clsid.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <guiddef.h>

namespace styler::tap {

// {B3A1F27C-6E45-4D8A-9F31-0C7E5A2D91B4}
// Registration-free: the XAML diagnostics layer LoadLibrary's our DLL by path
// and calls our exported DllGetClassObject with this CLSID. Nothing is written
// to the registry.
inline constexpr CLSID CLSID_TaskbarStylerTap = {
    0xb3a1f27c,
    0x6e45,
    0x4d8a,
    {0x9f, 0x31, 0x0c, 0x7e, 0x5a, 0x2d, 0x91, 0xb4}};

}  // namespace styler::tap
```

- [ ] **Step 4: Escrever `log.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace styler::tap {

enum class LogLevel { Error = 0, Info = 1, Debug = 2 };

void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();

// Called on the hot path: OnVisualTreeChange fires once per element, hundreds
// of times when the taskbar opens. Must stay a plain integer comparison.
bool LogEnabled(LogLevel level);

void LogLine(LogLevel level, std::wstring_view line);

// wsprintf-style. Never call directly; use STYLER_LOG, which checks the level
// BEFORE evaluating the arguments.
void LogLineFormatted(LogLevel level, const wchar_t* fmt, ...);

}  // namespace styler::tap

// The guard is the point: without it, a Debug call pays for argument
// evaluation and formatting even when logging is off.
#define STYLER_LOG(level, ...)                                   \
    do {                                                         \
        if (::styler::tap::LogEnabled(level)) {                  \
            ::styler::tap::LogLineFormatted(level, __VA_ARGS__); \
        }                                                        \
    } while (0)
```

- [ ] **Step 5: Escrever `log.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/log.h>

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace styler::tap {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Error};
std::mutex g_file_mutex;

constexpr long long kMaxLogBytes = 1024 * 1024;

std::wstring LogPath() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    std::wstring dir = std::wstring(base) + L"\\TaskbarStyler";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\log.txt";
}

const wchar_t* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Error:
            return L"ERR ";
        case LogLevel::Info:
            return L"INFO";
        case LogLevel::Debug:
            return L"DBG ";
    }
    return L"????";
}

// One rotation, not a series: the old file is replaced. A styling tool does not
// need log archaeology, and unbounded growth inside explorer.exe is worse than
// losing yesterday's lines.
void RotateIfLarge(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return;
    }
    long long size =
        (static_cast<long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size < kMaxLogBytes) {
        return;
    }
    std::wstring old = path + L".1";
    DeleteFileW(old.c_str());
    MoveFileW(path.c_str(), old.c_str());
}

}  // namespace

void SetLogLevel(LogLevel level) {
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel GetLogLevel() {
    return g_level.load(std::memory_order_relaxed);
}

bool LogEnabled(LogLevel level) {
    return static_cast<int>(level) <= static_cast<int>(GetLogLevel());
}

void LogLine(LogLevel level, std::wstring_view line) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t prefix[96]{};
    swprintf_s(prefix, L"[%02d:%02d:%02d.%03d %lu/%lu] %s ", st.wHour,
               st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(),
               GetCurrentThreadId(), LevelTag(level));

    std::wstring full = std::wstring(prefix) + std::wstring(line) + L"\n";

    // Always available, free when nobody listens, visible live in DebugView.
    OutputDebugStringW(full.c_str());

    static const std::wstring path = LogPath();
    if (path.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_file_mutex);
    RotateIfLarge(path);

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a+, ccs=UTF-8") == 0 && f) {
        fputws(full.c_str(), f);
        fclose(f);
    }
}

void LogLineFormatted(LogLevel level, const wchar_t* fmt, ...) {
    wchar_t buf[1024]{};

    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    LogLine(level, buf);
}

}  // namespace styler::tap
```

- [ ] **Step 6: Escrever os arquivos de build**

`src/tap/tap.def`:

```
LIBRARY TaskbarStyler.Tap
EXPORTS
    DllGetClassObject   PRIVATE
    DllCanUnloadNow     PRIVATE
```

`src/tap/CMakeLists.txt`:

```cmake
# The static half holds everything testable off-target; the MODULE half adds
# the COM boundary and is what gets loaded into explorer.exe.
add_library(taskbar_styler_tap_lib STATIC
  log.cpp
)
target_include_directories(taskbar_styler_tap_lib PUBLIC ..)

add_library(taskbar_styler_tap MODULE
  log.cpp
)
target_include_directories(taskbar_styler_tap PUBLIC ..)
set_target_properties(taskbar_styler_tap PROPERTIES
  OUTPUT_NAME "TaskbarStyler.Tap"
  PREFIX ""
  SUFFIX ".dll"
)

if(MSVC)
  target_compile_options(taskbar_styler_tap_lib PRIVATE /utf-8 /W4 /permissive-)
  target_compile_options(taskbar_styler_tap PRIVATE /utf-8 /W4 /permissive-)
  target_link_options(taskbar_styler_tap PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/tap.def")
endif()
```

`src/cli/CMakeLists.txt`:

```cmake
add_executable(taskbar_styler_cli
  main.cpp
)
target_include_directories(taskbar_styler_cli PRIVATE ..)
set_target_properties(taskbar_styler_cli PROPERTIES OUTPUT_NAME "taskbar-styler")
target_link_libraries(taskbar_styler_cli PRIVATE taskbar_styler_tap_lib)

if(MSVC)
  target_compile_options(taskbar_styler_cli PRIVATE /utf-8 /W4 /permissive-)
endif()
```

`tests/tap/CMakeLists.txt`:

```cmake
add_executable(taskbar_styler_tap_tests
  test_log.cpp
)
target_link_libraries(taskbar_styler_tap_tests PRIVATE
  taskbar_styler_tap_lib
  doctest::doctest
)
target_compile_definitions(taskbar_styler_tap_tests PRIVATE
  DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
)
if(MSVC)
  target_compile_options(taskbar_styler_tap_tests PRIVATE /utf-8 /W4 /permissive-)
endif()

add_test(NAME tap COMMAND taskbar_styler_tap_tests)
```

No `CMakeLists.txt` da raiz, depois de `add_subdirectory(src/core)`:

```cmake
add_subdirectory(src/tap)
add_subdirectory(src/cli)
```

e dentro do `if(TASKBAR_STYLER_BUILD_TESTS)`, depois de
`add_subdirectory(tests/core)`:

```cmake
  add_subdirectory(tests/tap)
```

`src/cli/main.cpp`, esqueleto:

```cpp
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
```

- [ ] **Step 7: Rodar e ver passar**

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Esperado: dois testes registrados (`core` e `tap`), ambos passando. Confirme que
`build/src/tap/TaskbarStyler.Tap.dll` e `build/src/cli/taskbar-styler.exe`
existem.

Nota de ambiente: `cmake`, `ninja` e `cl` não estão no PATH. Escreva um `.bat`
temporário que faça
`call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul`
seguido dos comandos, invoque-o por PowerShell, e apague o `.bat` antes de
commitar. Dois avisos são inofensivos e não devem ser perseguidos:
`'vswhere.exe' não é reconhecido` durante o vcvars, e `C5285` vindo do
`doctest.h`.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/tap src/cli tests/tap
git commit -m "build: alvos do TAP e do CLI, CLSID e log com niveis"
```

---

### Task 2: O TAP como objeto COM que o XAML consegue criar

**Files:**
- Create: `src/tap/tap_boundary.cpp`
- Modify: `src/tap/CMakeLists.txt` — somar `tap_boundary.cpp` **apenas** ao alvo
  `taskbar_styler_tap` (a DLL), nunca à lib estática: ele define exports COM.

**Interfaces:**
- Consumes: `CLSID_TaskbarStylerTap`, `STYLER_LOG`.
- Produces: os exports `DllGetClassObject` e `DllCanUnloadNow`; a variável
  `styler::tap::g_site` (`IUnknown*`), lida pela Task 3.

- [ ] **Step 1: Escrever `tap_boundary.cpp`**

Não há teste unitário possível: é COM dentro do explorer. A verificação é o smoke
test da Task 3.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// EVERY function here is callable by the XAML diagnostics layer from inside
// explorer.exe. An escaping exception kills the user's desktop, so each one is
// a catch-all that never propagates and returns S_OK even on failure -
// returning an error makes XAML stop sending events (upstream mirrors this,
// vendor/upstream/windows-11-taskbar-styler.wh.cpp:10604).
//
// Nothing else belongs in this file. "Is the boundary protected?" must be a
// question answered by opening one file.

#include <windows.h>
#include <inspectable.h>
#include <ocidl.h>

#include <new>

#include <tap/clsid.h>
#include <tap/log.h>

namespace styler::tap {

IUnknown* g_site = nullptr;

namespace {

class TaskbarStylerTap : public IObjectWithSite {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IObjectWithSite) {
            *ppv = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        try {
            STYLER_LOG(LogLevel::Info, L"SetSite(%p)", site);

            if (g_site) {
                g_site->Release();
                g_site = nullptr;
            }
            if (!site) {
                return S_OK;
            }

            site->AddRef();
            g_site = site;

            wchar_t host[MAX_PATH]{};
            GetModuleFileNameW(nullptr, host, MAX_PATH);
            STYLER_LOG(LogLevel::Info, L"loaded into %s", host);
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"SetSite threw");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppv) override {
        try {
            if (!g_site) {
                if (ppv) {
                    *ppv = nullptr;
                }
                return E_FAIL;
            }
            return g_site->QueryInterface(riid, ppv);
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"GetSite threw");
            return E_FAIL;
        }
    }

private:
    LONG m_ref = 1;
};

class TapFactory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid,
                                             void** ppv) override {
        try {
            if (outer) {
                return CLASS_E_NOAGGREGATION;
            }
            auto* tap = new (std::nothrow) TaskbarStylerTap();
            if (!tap) {
                return E_OUTOFMEMORY;
            }
            HRESULT hr = tap->QueryInterface(riid, ppv);
            tap->Release();
            return hr;
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"CreateInstance threw");
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }

private:
    LONG m_ref = 1;
};

}  // namespace
}  // namespace styler::tap

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    try {
        if (rclsid != styler::tap::CLSID_TaskbarStylerTap) {
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        auto* factory = new (std::nothrow) styler::tap::TapFactory();
        if (!factory) {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    } catch (...) {
        return E_FAIL;
    }
}

STDAPI DllCanUnloadNow() {
    // Deliberately never unloadable. Tearing a COM DLL out of a live process
    // with XAML callbacks possibly in flight is fragile; spec section 6.4 says
    // so plainly, and the CLI's `unload` explains it instead of faking it.
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
```

- [ ] **Step 2: Compilar e conferir os exports**

```
cmake --build build
dumpbin /exports build\src\tap\TaskbarStyler.Tap.dll
```

Esperado: `DllCanUnloadNow` e `DllGetClassObject` na lista de exports.

- [ ] **Step 3: Commit**

```bash
git add src/tap
git commit -m "feat(tap): objeto COM registration-free com a fronteira isolada"
```

---

### Task 3: O CLI carrega o TAP no explorer

Ao fim desta tarefa você roda um comando e vê, no log, uma linha escrita de
dentro do `explorer.exe`.

**Files:**
- Create: `src/cli/loader.h`, `src/cli/loader.cpp`
- Create: `tests/tap/test_loader.cpp`
- Modify: `src/cli/main.cpp`, `src/cli/CMakeLists.txt`,
  `tests/tap/CMakeLists.txt`

**Interfaces:**
- Consumes: `CLSID_TaskbarStylerTap`.
- Produces:
  - `struct styler::cli::LoadResult { HRESULT hr; DWORD pid; std::wstring connection; }`
  - `DWORD styler::cli::FindTaskbarPid()` — 0 se não achar.
  - `std::wstring styler::cli::TapDllPath()` — vazio se a DLL não estiver ao lado
    do exe.
  - `LoadResult styler::cli::LoadTap(DWORD pid, const std::wstring& tap_path)`
  - `std::wstring styler::cli::DescribeHresult(HRESULT)` — pura, testável.

- [ ] **Step 1: Escrever o teste falhando**

`tests/tap/test_loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cli/loader.h>

using styler::cli::DescribeHresult;

TEST_CASE("names the HRESULTs a user will actually hit") {
    CHECK(DescribeHresult(S_OK).find(L"S_OK") != std::wstring::npos);

    auto not_found = DescribeHresult(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    CHECK(not_found.find(L"ERROR_NOT_FOUND") != std::wstring::npos);

    auto denied = DescribeHresult(E_ACCESSDENIED);
    CHECK(denied.find(L"E_ACCESSDENIED") != std::wstring::npos);
}

TEST_CASE("an unknown HRESULT still carries its numeric value") {
    auto odd = DescribeHresult(static_cast<HRESULT>(0x8007ABCD));
    CHECK(odd.find(L"0x8007ABCD") != std::wstring::npos);
}
```

- [ ] **Step 2: Rodar e ver falhar**

```
cmake --build build
```

Esperado: FALHA, `cannot open source file "cli/loader.h"`.

- [ ] **Step 3: Escrever `loader.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>

namespace styler::cli {

struct LoadResult {
    HRESULT hr = E_FAIL;
    DWORD pid = 0;
    std::wstring connection;  // The VisualDiagConnection name that took.
};

// PID of the explorer.exe that owns the taskbar, or 0.
DWORD FindTaskbarPid();

// Absolute path to TaskbarStyler.Tap.dll, expected next to this executable.
// Empty when it is not there.
std::wstring TapDllPath();

// Asks the XAML framework inside `pid` to load our TAP. This is the whole
// trick: the OS does the loading, so no injection API is used anywhere.
LoadResult LoadTap(DWORD pid, const std::wstring& tap_path);

std::wstring DescribeHresult(HRESULT hr);

}  // namespace styler::cli
```

- [ ] **Step 4: Escrever `loader.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cli/loader.h>

#include <cstdio>

#include <tap/clsid.h>

namespace styler::cli {
namespace {

using PfnInitializeXamlDiagnosticsEx =
    HRESULT(WINAPI*)(PCWSTR endPointName, DWORD pid,
                     PCWSTR wszDllXamlDiagnostics, PCWSTR wszTAPDllName,
                     CLSID tapClsid, PCWSTR wszInitializationData);

// The XAML framework hands out a fixed set of diagnostic connection slots.
// Visual Studio's Live Visual Tree occupies one while attached, so a free slot
// has to be searched for. Upstream does the same.
constexpr int kMaxConnectionAttempts = 10000;

}  // namespace

DWORD FindTaskbarPid() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return 0;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    return pid;
}

std::wstring TapDllPath() {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        return {};
    }
    std::wstring path(exe);
    auto slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return {};
    }
    path.replace(slash + 1, std::wstring::npos, L"TaskbarStyler.Tap.dll");

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    return path;
}

LoadResult LoadTap(DWORD pid, const std::wstring& tap_path) {
    LoadResult result;
    result.pid = pid;

    HMODULE wux = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wux) {
        result.hr = HRESULT_FROM_WIN32(GetLastError());
        return result;
    }

    auto ixde = reinterpret_cast<PfnInitializeXamlDiagnosticsEx>(
        GetProcAddress(wux, "InitializeXamlDiagnosticsEx"));
    if (!ixde) {
        result.hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return result;
    }

    const HRESULT kNotFound = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    for (int i = 0; i < kMaxConnectionAttempts; i++) {
        wchar_t connection[64]{};
        swprintf_s(connection, L"VisualDiagConnection%d", i + 1);

        HRESULT hr = ixde(connection, pid, L"", tap_path.c_str(),
                          styler::tap::CLSID_TaskbarStylerTap, nullptr);

        if (hr == kNotFound) {
            continue;  // Slot busy; try the next.
        }

        result.hr = hr;
        result.connection = connection;
        return result;
    }

    result.hr = kNotFound;
    return result;
}

std::wstring DescribeHresult(HRESULT hr) {
    const wchar_t* name = nullptr;

    if (hr == S_OK) {
        name = L"S_OK";
    } else if (hr == E_ACCESSDENIED) {
        name = L"E_ACCESSDENIED";
    } else if (hr == E_INVALIDARG) {
        name = L"E_INVALIDARG";
    } else if (hr == E_NOINTERFACE) {
        name = L"E_NOINTERFACE";
    } else if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        name = L"ERROR_NOT_FOUND";
    } else if (hr == HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)) {
        name = L"ERROR_PROC_NOT_FOUND";
    }

    wchar_t buf[128]{};
    if (name) {
        swprintf_s(buf, L"0x%08X (%s)", static_cast<unsigned>(hr), name);
    } else {
        swprintf_s(buf, L"0x%08X", static_cast<unsigned>(hr));
    }
    return buf;
}

}  // namespace styler::cli
```

- [ ] **Step 5: Ligar o `load` no `main.cpp`**

Substitua `src/cli/main.cpp` inteiro por:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <cstdio>
#include <cstring>

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

    auto r = styler::cli::LoadTap(pid, tap);

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

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return Usage();
    }
    if (wcscmp(argv[1], L"load") == 0) {
        return CmdLoad();
    }
    wprintf(L"comando desconhecido: %s\n\n", argv[1]);
    return Usage();
}
```

Some `loader.cpp` às fontes de `taskbar_styler_cli`. Em
`tests/tap/CMakeLists.txt`, o alvo de teste precisa do `loader.cpp` e do include
path de `src/`:

```cmake
add_executable(taskbar_styler_tap_tests
  test_log.cpp
  test_loader.cpp
  ${CMAKE_SOURCE_DIR}/src/cli/loader.cpp
)
target_include_directories(taskbar_styler_tap_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
```

mantendo o `target_link_libraries` e as opções que já estão lá.

- [ ] **Step 6: Rodar os testes**

```
cmake --build build
ctest --test-dir build --output-on-failure
```

Esperado: tudo verde.

- [ ] **Step 7: Smoke test — a verificação que importa**

```
build\src\cli\taskbar-styler.exe load
```

Esperado: `carregado via VisualDiagConnection1` (ou outro número).

Abra `%LOCALAPPDATA%\TaskbarStyler\log.txt`. Ele **precisa** conter uma linha
`SetSite` cujo PID seja o do explorer, e uma linha
`loaded into C:\WINDOWS\Explorer.EXE`. Se o log não existe, ou o PID é o do CLI,
a DLL não entrou — pare e reporte, não siga.

Cole a saída real no relatório.

- [ ] **Step 8: Commit**

```bash
git add src/cli tests/tap
git commit -m "feat(cli): comando load, sem injecao e sem retry em loop"
```

---

### Task 4: Abrir a sessão de diagnóstico

Com o site na mão, obter `IXamlDiagnostics` e, quando disponível,
`IXamlDiagnosticsTestHooks` — a interface privada usada para liberar handles.
Aqui aparece o bookkeeping de handles, que se for esquecido faz o explorer
vazar memória continuamente (spec §7.2).

**Não é feita** aqui nenhuma assinatura de notificação de mudança
(`IVisualTreeServiceCallback2` / `AdviseVisualTreeChange`). Uma revisão de
código (fix round 1) encontrou três Criticals na versão original desta tarefa,
todos decorrentes de assinar notificações sem ter onde drená-las com segurança:
liberar um handle de dentro de `OnVisualTreeChange` acontece enquanto o Leave
walk do explorer ainda está visitando a subárvore sendo removida — o upstream
chama isso de "the one thing that isn't safe" e resolve enfileirando a
liberação e drenando na thread do dispatcher
(`vendor/upstream/windows-11-taskbar-styler.wh.cpp:11168`, `:18379`, `:18404`).
Nada no Plano 2 consome esse fluxo por elemento de qualquer forma — a Task 5
percorre a árvore sob demanda via `IVisualTreeService3::GetVisualRoots`/
`GetChildren`. A assinatura de notificações, e o dreno de liberação adiada que
ela exige, ficam para o Plano 3, que é o primeiro a precisar de notificação de
mudança ao vivo para aplicar estilo incrementalmente conforme elementos
aparecem.

**Files:**
- Create: `src/tap/visual_tree_watcher.h`, `src/tap/visual_tree_watcher.cpp`
- Modify: `src/tap/tap_boundary.cpp`, `src/tap/CMakeLists.txt`

**Interfaces:**
- Consumes: `g_site`, `STYLER_LOG`.
- Produces:
  - `HRESULT styler::tap::OpenDiagnostics(IUnknown* site)`
  - `void styler::tap::CloseDiagnostics()`
  - `void styler::tap::ReleaseHandle(InstanceHandle handle)` — usado pela
    Task 5 para liberar cada handle que sua travessia obtiver de
    `GetChildren`; no-op para `handle == 0` (o handle de pai de uma raiz).
  - `long styler::tap::LiveHandleCount()` — conta quantos handles já foram
    liberados com sucesso neste processo; monotônico (só cresce), para servir
    de observável de vazamento (spec §7.2) — um contador que pode cair abaixo
    de zero nunca conseguiria mostrar um vazamento parado.
  - `IXamlDiagnostics* styler::tap::Diagnostics()` — usado pela Task 5; válido
    somente entre um `OpenDiagnostics` bem-sucedido e o `CloseDiagnostics`
    (ou `SetSite(nullptr)`) seguinte.

- [ ] **Step 1: Conferir o GUID de `IXamlDiagnosticsTestHooks`**

O valor já está no Step 3 abaixo, lido do upstream. Confirme que continua igual
antes de confiar nele:

```
grep -n -A2 "IID_IXamlDiagnosticsTestHooks =" vendor/upstream/windows-11-taskbar-styler.wh.cpp
```

Esperado: `{0x735941a2, 0x3ee3, 0x495a, {0x8d, 0xa9, 0x97, 0x26, 0x27, 0x00, 0x30, 0x75}}`
na linha 10946. Se divergir, use o do upstream e reporte — sem esse GUID cada
elemento reportado vaza pela vida inteira do processo explorer.

- [ ] **Step 2: Escrever `visual_tree_watcher.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

namespace styler::tap {

// Holds the diagnostics session against a XAML host: the QI'd IXamlDiagnostics
// and, when available, the private IXamlDiagnosticsTestHooks used to release
// handles.
//
// This file does NOT subscribe to change notifications
// (IVisualTreeServiceCallback2 / AdviseVisualTreeChange). Releasing a handle
// from inside that callback is unsafe: the report arrives from inside
// explorer's Leave walk, which is still visiting the subtree being removed,
// so releasing there destroys it mid-walk - upstream calls this out as "the
// one thing that isn't safe" and solves it by queueing the release and
// draining the queue on the host's dispatcher thread
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:11168, :18379, :18404).
// Plano 2 has no such drain and nothing here consumes a per-element change
// stream anyway (Task 5 walks the tree on demand through
// IVisualTreeService3::GetVisualRoots/GetChildren) - the subscription, and
// the deferred-release drain it requires, is deferred to Plano 3, which is
// the first plan that actually needs live change notifications.

// Opens the diagnostics session against `site`'s IXamlDiagnostics. If a
// session is already open, it is closed first. Returns a real HRESULT; on
// any failure no session is left open (Diagnostics() returns nullptr).
HRESULT OpenDiagnostics(IUnknown* site);

// Closes the session opened by OpenDiagnostics, if any. Safe to call when no
// session is open.
void CloseDiagnostics();

// The open session's IXamlDiagnostics, or nullptr if no session is open (no
// OpenDiagnostics call yet, it failed, or CloseDiagnostics ran since). This
// is a live, non-owning pointer: the caller does not Release it, and must not
// cache it past a call that could race a concurrent CloseDiagnostics - AddRef
// a private copy to hold it longer than one call (same rule as SiteOrNull(),
// see site.h).
IXamlDiagnostics* Diagnostics();

// Releases one handle the diagnostics layer reported - e.g. a handle Task 5's
// tree walk got back from IVisualTreeService3::GetChildren. Every handle the
// diagnostics layer hands out stays registered on its side and explorer.exe
// leaks for as long as it runs until this is called (spec section 7.2). Safe
// to call with handle == 0 (a root element's parent handle): that is not a
// real handle, and the underlying vtable is private and undocumented, not
// something to probe with a null handle. Also a safe no-op while no session
// is open, or IXamlDiagnosticsTestHooks is unavailable (warned once, in
// OpenDiagnostics).
void ReleaseHandle(InstanceHandle handle);

// Count of handles successfully released so far this process, via
// ReleaseHandle. Monotonic - it only increases - so it can serve as the leak
// observable spec section 7.2 asks for: a counter that could drift negative
// could never surface a stalled release path the way this one can.
long LiveHandleCount();

}  // namespace styler::tap
```

- [ ] **Step 3: Escrever `visual_tree_watcher.cpp`**

Substitua o GUID placeholder abaixo pelo que você leu no Step 1.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/visual_tree_watcher.h>

#include <atomic>
#include <new>

#include <tap/log.h>

namespace styler::tap {
namespace {

// {735941A2-3EE3-495A-8DA9-972627003075}
// Private and undocumented; read from the vendored upstream at line 10946.
// Confirmed to still match before trusting this constant (2026-09-12).
constexpr GUID IID_IXamlDiagnosticsTestHooks = {
    0x735941a2,
    0x3ee3,
    0x495a,
    {0x8d, 0xa9, 0x97, 0x26, 0x27, 0x00, 0x30, 0x75}};

struct IXamlDiagnosticsTestHooks : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE UnregisterInstance(
        InstanceHandle handle) = 0;
};

// Owns one open session's IXamlDiagnostics and (when available)
// IXamlDiagnosticsTestHooks. Both are released exactly once, from the
// destructor, so teardown has a single gate instead of Release() calls
// scattered across failure paths that one of them could skip.
class DiagnosticsSession {
public:
    DiagnosticsSession(IXamlDiagnostics* diagnostics,
                        IXamlDiagnosticsTestHooks* hooks)
        : diagnostics_(diagnostics), hooks_(hooks) {}

    DiagnosticsSession(const DiagnosticsSession&) = delete;
    DiagnosticsSession& operator=(const DiagnosticsSession&) = delete;

    ~DiagnosticsSession() {
        if (hooks_) {
            hooks_->Release();
        }
        if (diagnostics_) {
            diagnostics_->Release();
        }
    }

    IXamlDiagnostics* diagnostics() const { return diagnostics_; }
    IXamlDiagnosticsTestHooks* hooks() const { return hooks_; }

private:
    IXamlDiagnostics* diagnostics_;
    IXamlDiagnosticsTestHooks* hooks_;
};

// The current session, or nullptr. Written only by OpenDiagnostics/
// CloseDiagnostics below; read through Diagnostics()/ReleaseHandle - never
// reach for this directly from another translation unit. std::atomic because
// the TAP is called from several explorer UI threads (same reasoning as
// g_site in tap_boundary.cpp - see site.h).
std::atomic<DiagnosticsSession*> g_session{nullptr};

// Handles successfully released so far this process. Monotonic by
// construction: only ReleaseHandle's success path touches it, and only ever
// increments it.
std::atomic<long> g_released_handles{0};

}  // namespace

void ReleaseHandle(InstanceHandle handle) {
    if (!handle) {
        return;  // A root element's parent handle is 0; nothing to release.
    }

    DiagnosticsSession* session = g_session.load(std::memory_order_acquire);
    if (!session || !session->hooks()) {
        return;  // No session open, or hooks unavailable - warned once above.
    }

    HRESULT hr = session->hooks()->UnregisterInstance(handle);
    if (SUCCEEDED(hr)) {
        g_released_handles.fetch_add(1, std::memory_order_relaxed);
    } else {
        STYLER_LOG(LogLevel::Error, L"UnregisterInstance(%llu) failed 0x%08X",
                   static_cast<unsigned long long>(handle),
                   static_cast<unsigned>(hr));
    }
}

long LiveHandleCount() {
    return g_released_handles.load(std::memory_order_relaxed);
}

IXamlDiagnostics* Diagnostics() {
    DiagnosticsSession* session = g_session.load(std::memory_order_acquire);
    return session ? session->diagnostics() : nullptr;
}

HRESULT OpenDiagnostics(IUnknown* site) {
    if (!site) {
        return E_INVALIDARG;
    }

    if (g_session.load(std::memory_order_acquire)) {
        STYLER_LOG(LogLevel::Info,
                   L"OpenDiagnostics called with a session already open; "
                   L"closing and reopening");
        CloseDiagnostics();
    }

    IXamlDiagnostics* diagnostics = nullptr;
    HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&diagnostics));
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"QI IXamlDiagnostics failed 0x%08X",
                   static_cast<unsigned>(hr));
        return hr;
    }

    IXamlDiagnosticsTestHooks* hooks = nullptr;
    if (FAILED(diagnostics->QueryInterface(
            IID_IXamlDiagnosticsTestHooks,
            reinterpret_cast<void**>(&hooks)))) {
        // Not fatal, but the user must know: without it every reported
        // element leaks for the life of the explorer process.
        STYLER_LOG(LogLevel::Error,
                   L"IXamlDiagnosticsTestHooks unavailable - elements will "
                   L"leak; report this, it means Windows changed");
        hooks = nullptr;
    }

    auto* session = new (std::nothrow) DiagnosticsSession(diagnostics, hooks);
    if (!session) {
        if (hooks) {
            hooks->Release();
        }
        diagnostics->Release();
        return E_OUTOFMEMORY;
    }

    g_session.store(session, std::memory_order_release);
    STYLER_LOG(LogLevel::Info, L"diagnostics session open");
    return S_OK;
}

void CloseDiagnostics() {
    DiagnosticsSession* session =
        g_session.exchange(nullptr, std::memory_order_acq_rel);
    if (!session) {
        return;
    }
    delete session;  // Releases hooks and diagnostics exactly once.
    STYLER_LOG(LogLevel::Info, L"diagnostics session closed");
}

}  // namespace styler::tap
```

- [ ] **Step 4: Chamar do `SetSite`**

Em `tap_boundary.cpp`, inclua `<tap/visual_tree_watcher.h>`. Dentro de `SetSite`,
logo depois de `STYLER_LOG(LogLevel::Info, L"loaded into %s", host);`, some:

```cpp
            HRESULT hr = OpenDiagnostics(site);
            if (FAILED(hr)) {
                STYLER_LOG(LogLevel::Error, L"OpenDiagnostics failed 0x%08X",
                           static_cast<unsigned>(hr));
            }
```

e no ramo `if (!site)`, antes do `return S_OK`, some `CloseDiagnostics();`.

- [ ] **Step 5: Compilar e fazer o smoke test**

```
cmake --build build
build\src\cli\taskbar-styler.exe load
```

Esperado no log: `diagnostics session open`, e **nenhuma** linha
`IXamlDiagnosticsTestHooks unavailable`. Se ela aparecer, o GUID está errado —
volte ao Step 1. Não há mais linha `watching the visual tree`: nada é
assinado, então não há o que "vigiar" (ver a nota acima sobre o que fica para
o Plano 3).

Depois reinicie o Explorador pelo Gerenciador de Tarefas para descarregar a DLL.

- [ ] **Step 6: Commit**

```bash
git add src/tap
git commit -m "feat(tap): abre a sessao de diagnostico e libera os handles"
```

---

### Task 5: Exportar a árvore

A entrega do plano.

**Files:**
- Create: `src/tap/tree_export.h`, `src/tap/tree_export.cpp`
- Create: `tests/tap/test_tree_format.cpp`
- Modify: `src/tap/tap_boundary.cpp`, `src/tap/CMakeLists.txt`,
  `tests/tap/CMakeLists.txt`

**Interfaces:**
- Consumes: `Diagnostics()`, `ReleaseHandle`, `STYLER_LOG`. `Diagnostics()` só é
  válido entre um `OpenDiagnostics` bem-sucedido (Task 4) e o
  `CloseDiagnostics`/`SetSite(nullptr)` seguinte — verifique-o por `nullptr`
  antes de usar. Cada handle que a travessia obtém de `GetVisualRoots`/
  `GetChildren` precisa ser passado a `ReleaseHandle` depois de usado (Task 4),
  senão vaza pela vida do processo explorer (spec §7.2) — a travessia sob
  demanda não ganha essa liberação de graça só por o Plano 2 ter aberto a
  sessão de diagnóstico.
- Produces:
  - `struct styler::tap::TreeNode { std::wstring type; std::wstring name; int one_based_index; std::vector<TreeNode> children; }`
  - `std::wstring styler::tap::FormatTree(const TreeNode& root)` — **pura**.
  - `HRESULT styler::tap::ExportTreeToFile(const std::wstring& path)`

`FormatTree` e `AssignSiblingIndices` vão para a lib estática, para poderem ser
testados sem XAML; `ExportTreeToFile` só na DLL.

- [ ] **Step 1: Escrever o teste da formatação**

`tests/tap/test_tree_format.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/tree_export.h>

using styler::tap::AssignSiblingIndices;
using styler::tap::FormatTree;
using styler::tap::TreeNode;

TEST_CASE("formats a single node") {
    TreeNode root;
    root.type = L"Taskbar.TaskbarFrame";
    CHECK(FormatTree(root) == L"Taskbar.TaskbarFrame\n");
}

TEST_CASE("appends #Name when the element has one") {
    TreeNode root;
    root.type = L"Grid";
    root.name = L"RootGrid";
    CHECK(FormatTree(root) == L"Grid#RootGrid\n");
}

TEST_CASE("indents two spaces per level") {
    TreeNode root;
    root.type = L"A";
    TreeNode b;
    b.type = L"B";
    TreeNode c;
    c.type = L"C";
    c.name = L"Deep";
    b.children.push_back(c);
    root.children.push_back(b);

    CHECK(FormatTree(root) ==
          L"A\n"
          L"  B\n"
          L"    C#Deep\n");
}

TEST_CASE("indexes only the sibling types that repeat") {
    TreeNode root;
    root.type = L"Grid";
    TreeNode r1;
    r1.type = L"Rectangle";
    TreeNode r2;
    r2.type = L"Rectangle";
    TreeNode border;
    border.type = L"Border";
    root.children = {r1, r2, border};

    AssignSiblingIndices(root);

    CHECK(FormatTree(root) ==
          L"Grid\n"
          L"  Rectangle[1]\n"
          L"  Rectangle[2]\n"
          L"  Border\n");
}
```

- [ ] **Step 2: Rodar e ver falhar**

```
cmake --build build
```

Esperado: FALHA, `cannot open source file "tap/tree_export.h"`.

- [ ] **Step 3: Escrever `tree_export.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace styler::tap {

// A snapshot of one element, in the same vocabulary the theme selectors use, so
// a line of the export can be pasted into a theme JSON with no translation.
struct TreeNode {
    std::wstring type;
    std::wstring name;
    int one_based_index = 0;  // 0 when the element needs no index to be unique.
    std::vector<TreeNode> children;
};

// Fills one_based_index only where a type repeats among siblings. An index on a
// unique child would be noise in a selector. Pure.
void AssignSiblingIndices(TreeNode& parent);

// Pure: no XAML, no COM. This is the part worth unit testing.
std::wstring FormatTree(const TreeNode& root);

// Walks the live visual tree and writes the formatted result.
HRESULT ExportTreeToFile(const std::wstring& path);

}  // namespace styler::tap
```

- [ ] **Step 4: Escrever a parte pura, `tree_format.cpp`**

Arquivo novo `src/tap/tree_format.cpp`, que entra **na lib estática e na DLL**:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <cstdio>

namespace styler::tap {
namespace {

void AppendNode(std::wstring& out, const TreeNode& node, int depth) {
    out.append(static_cast<size_t>(depth) * 2, L' ');
    out += node.type;

    if (!node.name.empty()) {
        out += L'#';
        out += node.name;
    }
    if (node.one_based_index > 0) {
        wchar_t idx[16]{};
        swprintf_s(idx, L"[%d]", node.one_based_index);
        out += idx;
    }
    out += L'\n';

    for (const auto& child : node.children) {
        AppendNode(out, child, depth + 1);
    }
}

}  // namespace

void AssignSiblingIndices(TreeNode& parent) {
    for (size_t i = 0; i < parent.children.size(); i++) {
        int seen = 0;
        int position = 0;
        for (size_t j = 0; j < parent.children.size(); j++) {
            if (parent.children[j].type == parent.children[i].type) {
                seen++;
                if (j == i) {
                    position = seen;
                }
            }
        }
        parent.children[i].one_based_index = (seen > 1) ? position : 0;
    }
    for (auto& child : parent.children) {
        AssignSiblingIndices(child);
    }
}

std::wstring FormatTree(const TreeNode& root) {
    std::wstring out;
    AppendNode(out, root, 0);
    return out;
}

}  // namespace styler::tap
```

Some `tree_format.cpp` a **ambos** os alvos em `src/tap/CMakeLists.txt`, e
`test_tree_format.cpp` ao alvo de teste.

- [ ] **Step 5: Rodar e ver passar**

```
cmake --build build
ctest --test-dir build --output-on-failure
```

Esperado: os quatro `TEST_CASE` novos passando.

- [ ] **Step 6: Escrever a travessia, `tree_export.cpp`**

Só na DLL:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <inspectable.h>
#include <xamlom.h>

#include <cstdio>

#include <tap/log.h>
#include <tap/visual_tree_watcher.h>

namespace styler::tap {
namespace {

constexpr int kMaxDepth = 64;

void BuildNode(IVisualTreeService3* service, InstanceHandle handle,
               TreeNode& out, int depth) {
    if (depth > kMaxDepth) {
        STYLER_LOG(LogLevel::Error, L"tree deeper than %d, stopping",
                   kMaxDepth);
        return;
    }

    unsigned int count = 0;
    InstanceHandle* children = nullptr;
    if (FAILED(service->GetChildren(handle, &count, &children))) {
        return;
    }

    for (unsigned int i = 0; i < count; i++) {
        VisualElement element{};
        if (FAILED(service->GetVisualElement(children[i], &element))) {
            continue;
        }

        TreeNode child;
        child.type = element.Type ? element.Type : L"<unknown>";
        child.name = element.Name ? element.Name : L"";

        BuildNode(service, children[i], child, depth + 1);
        out.children.push_back(std::move(child));

        // The diagnostics layer keeps this handle registered until we say
        // otherwise; every one GetChildren hands out must be released or it
        // leaks for the life of explorer.exe (spec section 7.2).
        ReleaseHandle(children[i]);
    }

    if (children) {
        CoTaskMemFree(children);
    }
}

}  // namespace

HRESULT ExportTreeToFile(const std::wstring& path) {
    IXamlDiagnostics* diagnostics = Diagnostics();
    if (!diagnostics) {
        return E_NOT_VALID_STATE;
    }

    IVisualTreeService3* service = nullptr;
    HRESULT hr = diagnostics->QueryInterface(IID_PPV_ARGS(&service));
    if (FAILED(hr)) {
        return hr;
    }

    unsigned int root_count = 0;
    InstanceHandle* roots = nullptr;
    hr = service->GetVisualRoots(&root_count, &roots);
    if (FAILED(hr)) {
        service->Release();
        return hr;
    }

    std::wstring out;
    for (unsigned int i = 0; i < root_count; i++) {
        VisualElement element{};
        if (FAILED(service->GetVisualElement(roots[i], &element))) {
            continue;
        }
        TreeNode root;
        root.type = element.Type ? element.Type : L"<root>";
        root.name = element.Name ? element.Name : L"";

        BuildNode(service, roots[i], root, 0);
        AssignSiblingIndices(root);

        out += FormatTree(root);
        out += L'\n';

        ReleaseHandle(roots[i]);  // Same rule as every child handle above.
    }

    if (roots) {
        CoTaskMemFree(roots);
    }
    service->Release();

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) {
        return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
    }
    fputws(out.c_str(), f);
    fclose(f);

    STYLER_LOG(LogLevel::Info, L"tree exported to %s", path.c_str());
    return S_OK;
}

}  // namespace styler::tap
```

- [ ] **Step 7: Disparar na carga**

Em `tap_boundary.cpp`, inclua `<tap/tree_export.h>` e `<string>`. Dentro de
`SetSite`, depois do `OpenDiagnostics` bem-sucedido, some:

```cpp
            wchar_t local[MAX_PATH]{};
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                std::wstring out =
                    std::wstring(local) + L"\\TaskbarStyler\\visual-tree.txt";
                ExportTreeToFile(out);
            }
```

- [ ] **Step 8: Smoke test — a entrega do plano**

```
cmake --build build
build\src\cli\taskbar-styler.exe load
```

Abra `%LOCALAPPDATA%\TaskbarStyler\visual-tree.txt`.

**Confira contra um tema real.** Abra `themes/TranslucentTaskbar.json` e pegue o
alvo
`Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill`.
Cada um desses elementos precisa aparecer na exportação, aninhado nessa ordem. Se
não aparecer, a travessia está errada ou incompleta — reporte colando o trecho da
exportação. **Não relaxe a verificação.**

Reinicie o Explorador para limpar.

- [ ] **Step 9: Commit**

```bash
git add src/tap tests/tap
git commit -m "feat(tap): exporta a arvore visual em formato de seletor"
```

---

### Task 6: Ciclo de vida — múltiplos hosts

Sem isto, o TAP só enxerga a taskbar principal.

**Files:**
- Create: `src/tap/thread_init.h`, `src/tap/thread_init.cpp`
- Modify: `src/tap/tap_boundary.cpp`, `src/tap/CMakeLists.txt`,
  `src/cli/main.cpp`

**Interfaces:**
- Consumes: `STYLER_LOG`.
- Produces:
  - `std::vector<HWND> styler::tap::GetXamlHostWnds()`
  - `HWND styler::tap::GetTaskbarUiWnd()`
  - `using ThreadProc = void(WINAPI*)(void*)`
  - `bool styler::tap::RunOnWindowThread(HWND, ThreadProc, void*)`
  - `void styler::tap::InitializeForCurrentThread()` / `UninitializeForCurrentThread()`
  - `bool styler::tap::IsInitializedForCurrentThread()`
  - `void styler::tap::StartHostWatch()` / `StopHostWatch()`

- [ ] **Step 1: Escrever `thread_init.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <vector>

namespace styler::tap {

// The taskbar is not one window. The main UI is a
// Windows.UI.Composition.DesktopWindowContentBridge child of Shell_TrayWnd;
// flyouts and secondary-monitor taskbars are XamlExplorerHostIslandWindow; the
// language switcher is Shell_InputSwitchTopLevelWindow. Each runs on its own
// thread and must be initialized from that thread.
std::vector<HWND> GetXamlHostWnds();
HWND GetTaskbarUiWnd();

using ThreadProc = void(WINAPI*)(void* parameter);

// Runs `proc` on the thread owning `hWnd`, via a WH_CALLWNDPROC hook and a
// registered message. Documented API - no code is patched.
bool RunOnWindowThread(HWND hWnd, ThreadProc proc, void* param);

void InitializeForCurrentThread();
void UninitializeForCurrentThread();
bool IsInitializedForCurrentThread();

// SetWinEventHook on EVENT_OBJECT_CREATE, filtered to this process. The OS
// tells us when a new window appears, so we never patch CreateWindowExW the way
// upstream does. This is the last place a code patch could have crept in - see
// spec section 6.2.
void StartHostWatch();
void StopHostWatch();

}  // namespace styler::tap
```

- [ ] **Step 2: Escrever `thread_init.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/thread_init.h>

#include <tap/log.h>

namespace styler::tap {
namespace {

thread_local bool t_initialized = false;
HWINEVENTHOOK g_host_hook = nullptr;

struct RunParam {
    ThreadProc proc;
    void* param;
};

UINT RunMessage() {
    static const UINT msg =
        RegisterWindowMessageW(L"TaskbarStyler_RunOnWindowThread");
    return msg;
}

BOOL CALLBACK EnumHostsProc(HWND hWnd, LPARAM lParam) {
    auto* out = reinterpret_cast<std::vector<HWND>*>(lParam);

    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hWnd, &pid) || pid != GetCurrentProcessId()) {
        return TRUE;
    }

    wchar_t cls[64]{};
    if (GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) == 0) {
        return TRUE;
    }

    if (_wcsicmp(cls, L"XamlExplorerHostIslandWindow") == 0 ||
        _wcsicmp(cls, L"Shell_InputSwitchTopLevelWindow") == 0) {
        out->push_back(hWnd);
    }
    return TRUE;
}

BOOL CALLBACK FindTrayProc(HWND hWnd, LPARAM lParam) {
    DWORD pid = 0;
    wchar_t cls[64]{};
    if (GetWindowThreadProcessId(hWnd, &pid) && pid == GetCurrentProcessId() &&
        GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) &&
        _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
        *reinterpret_cast<HWND*>(lParam) = hWnd;
        return FALSE;
    }
    return TRUE;
}

void WINAPI InitThunk(void*) {
    InitializeForCurrentThread();
}

void CALLBACK HostCreatedProc(HWINEVENTHOOK, DWORD event, HWND hWnd,
                              LONG idObject, LONG, DWORD, DWORD) {
    if (event != EVENT_OBJECT_CREATE || idObject != OBJID_WINDOW || !hWnd) {
        return;
    }

    wchar_t cls[64]{};
    if (GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) == 0) {
        return;
    }
    if (_wcsicmp(cls, L"XamlExplorerHostIslandWindow") != 0) {
        return;
    }

    STYLER_LOG(LogLevel::Info, L"new XAML host %p", hWnd);
    RunOnWindowThread(hWnd, InitThunk, nullptr);
}

}  // namespace

std::vector<HWND> GetXamlHostWnds() {
    std::vector<HWND> hosts;
    EnumWindows(EnumHostsProc, reinterpret_cast<LPARAM>(&hosts));
    return hosts;
}

HWND GetTaskbarUiWnd() {
    HWND tray = nullptr;
    EnumWindows(FindTrayProc, reinterpret_cast<LPARAM>(&tray));
    if (!tray) {
        return nullptr;
    }
    return FindWindowExW(tray, nullptr,
                         L"Windows.UI.Composition.DesktopWindowContentBridge",
                         nullptr);
}

bool RunOnWindowThread(HWND hWnd, ThreadProc proc, void* param) {
    DWORD thread_id = GetWindowThreadProcessId(hWnd, nullptr);
    if (thread_id == 0) {
        return false;
    }
    if (thread_id == GetCurrentThreadId()) {
        proc(param);
        return true;
    }

    RunParam rp{proc, param};

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                const auto* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (cwp->message == RunMessage()) {
                    auto* p = reinterpret_cast<RunParam*>(cwp->lParam);
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr, thread_id);

    if (!hook) {
        return false;
    }

    SendMessageW(hWnd, RunMessage(), 0, reinterpret_cast<LPARAM>(&rp));
    UnhookWindowsHookEx(hook);
    return true;
}

void InitializeForCurrentThread() {
    if (t_initialized) {
        return;
    }
    t_initialized = true;
    STYLER_LOG(LogLevel::Info, L"initialized for thread %lu",
               GetCurrentThreadId());
}

void UninitializeForCurrentThread() {
    if (!t_initialized) {
        return;
    }
    t_initialized = false;
    STYLER_LOG(LogLevel::Info, L"uninitialized for thread %lu",
               GetCurrentThreadId());
}

bool IsInitializedForCurrentThread() {
    return t_initialized;
}

void StartHostWatch() {
    if (g_host_hook) {
        return;
    }
    g_host_hook =
        SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
                        HostCreatedProc, GetCurrentProcessId(), 0,
                        WINEVENT_OUTOFCONTEXT);
    STYLER_LOG(LogLevel::Info, L"host watch %s",
               g_host_hook ? L"started" : L"FAILED to start");
}

void StopHostWatch() {
    if (!g_host_hook) {
        return;
    }
    UnhookWinEvent(g_host_hook);
    g_host_hook = nullptr;
}

}  // namespace styler::tap
```

- [ ] **Step 3: Ligar no `SetSite`**

Em `tap_boundary.cpp`, inclua `<tap/thread_init.h>`. Depois do `StartWatching` e
antes da exportação, some:

```cpp
            if (HWND ui = GetTaskbarUiWnd()) {
                RunOnWindowThread(ui, InitThunkPublic, nullptr);
            }
            for (HWND host : GetXamlHostWnds()) {
                RunOnWindowThread(host, InitThunkPublic, nullptr);
            }
            StartHostWatch();
```

e defina, no namespace anônimo de `tap_boundary.cpp`:

```cpp
void WINAPI InitThunkPublic(void*) {
    InitializeForCurrentThread();
}
```

No ramo `if (!site)`, antes de `StopWatching()`, some `StopHostWatch();`.

- [ ] **Step 4: `status` e `unload` no CLI**

Em `src/cli/main.cpp`, some as funções e as entradas no despacho de `wmain`:

```cpp
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
```

Adicione `#include <string>` ao `main.cpp`.

- [ ] **Step 5: Smoke test do ciclo de vida**

```
cmake --build build
build\src\cli\taskbar-styler.exe load
```

Esperado no log: uma linha `initialized for thread` para a UI principal e uma por
host XAML encontrado, mais `host watch started`.

Então, com o TAP carregado, abra o flyout de rede ou de bateria clicando no ícone
da bandeja. Esperado: `new XAML host` e mais uma linha `initialized for thread`.
Isso prova que o `SetWinEventHook` funciona e que nenhum patch em
`CreateWindowExW` foi necessário.

Reinicie o Explorador para limpar.

- [ ] **Step 6: Commit**

```bash
git add src/tap src/cli
git commit -m "feat(tap): ciclo de vida por thread e deteccao de host sem patch"
```

---

### Task 7: Checklist de smoke test, CI e README

Fecha a lacuna da spec §8.2.

**Files:**
- Create: `docs/smoke-test.md`
- Modify: `.github/workflows/ci.yml`, `README.md`

**Interfaces:** nenhuma em código.

- [ ] **Step 1: Escrever `docs/smoke-test.md`**

````markdown
# Checklist de smoke test

Um runner de CI não tem taskbar interativa, então o que segue é manual e
versionado. Rode antes de fechar qualquer plano que mexa no TAP.

Compile primeiro: `cmake --build build`.

## 1. Carga

- [ ] `taskbar-styler load` imprime `carregado via VisualDiagConnection<n>`
- [ ] `%LOCALAPPDATA%\TaskbarStyler\log.txt` contém `SetSite`
- [ ] a linha `loaded into` aponta para `C:\WINDOWS\Explorer.EXE`
- [ ] o PID nas linhas de log é o mesmo que o comando imprimiu
- [ ] **não** aparece `IXamlDiagnosticsTestHooks unavailable`

## 2. Árvore visual

- [ ] `%LOCALAPPDATA%\TaskbarStyler\visual-tree.txt` existe
- [ ] a cadeia
      `Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill`,
      tirada de `themes/TranslucentTaskbar.json`, aparece aninhada nessa ordem
- [ ] a indentação cresce dois espaços por nível

## 3. Múltiplos hosts

- [ ] o log tem `initialized for thread` para a UI principal
- [ ] tem `host watch started`
- [ ] abrir o flyout de rede produz `new XAML host` e mais um
      `initialized for thread`
- [ ] conectar um segundo monitor depois da carga produz mais um host

## 4. Estabilidade

- [ ] a taskbar continua respondendo normalmente
- [ ] nenhum diálogo aparece
- [ ] após dez minutos de uso, a memória do `explorer.exe` no Gerenciador de
      Tarefas não cresce continuamente

## 5. Limpeza

- [ ] reiniciar o Explorador pelo Gerenciador de Tarefas descarrega a DLL
- [ ] `taskbar-styler status` depois disso não trava nem mente
````

- [ ] **Step 2: Somar ao CI**

O CI não roda o smoke test, mas precisa garantir que os artefatos saem. Em
`.github/workflows/ci.yml`, depois do passo `Test`:

```yaml
      - name: Artefatos do TAP e do CLI existem
        shell: bash
        run: |
          test -f build/src/cli/TaskbarStyler.Tap.dll
          test -f build/src/cli/taskbar-styler.exe

      - name: Nenhuma API de injecao no codigo
        shell: bash
        run: |
          ! grep -rnE 'VirtualAllocEx|WriteProcessMemory|CreateRemoteThread|SetThreadContext' src/
```

- [ ] **Step 3: Atualizar o README**

Na seção `## Estado`, troque a lista por:

```markdown
- [x] `styler_core` — parsing de seletores, regras de estilo e temas
- [x] 55 temas em JSON, com conversão provada sem perda byte a byte
- [x] TAP que carrega no explorer e exporta a árvore visual
- [ ] Aplicar e desfazer estilos (Plano 3)
- [ ] O aplicativo de bandeja (Plano 4)
```

E acrescente, antes de `## Temas`:

````markdown
## Vendo a árvore visual

```
taskbar-styler load
```

Escreve `%LOCALAPPDATA%\TaskbarStyler\visual-tree.txt` com a árvore da sua
taskbar, no mesmo formato dos seletores dos temas. É com isso que você conserta
um tema sozinho quando uma atualização do Windows renomeia algum elemento.
````

- [ ] **Step 4: Rodar o checklist inteiro**

Execute `docs/smoke-test.md` de ponta a ponta e registre no relatório o resultado
de cada item. Um item que falhou é um achado, não um detalhe a arredondar.

- [ ] **Step 5: Commit**

```bash
git add docs/smoke-test.md .github/workflows/ci.yml README.md
git commit -m "docs: checklist de smoke test e verificacoes do TAP no CI"
```

---

## Definição de pronto do Plano 2

- `ctest` verde, incluindo os testes novos de log, HRESULT e formatação de árvore.
- `taskbar-styler load` carrega a DLL no `explorer.exe`, comprovado por linha de
  log escrita de dentro do processo.
- `visual-tree.txt` contém a cadeia de um tema real, aninhada na ordem certa.
- Flyout aberto depois da carga gera host novo inicializado.
- `docs/smoke-test.md` rodado inteiro, com o resultado de cada item registrado.
- CI verde, com os dois artefatos presentes e o grep anti-injeção passando.
- **Nenhuma ocorrência** de `VirtualAllocEx`, `WriteProcessMemory`,
  `CreateRemoteThread`, `SetThreadContext`, MinHook ou Detours em todo o `src/`.

Nada aplica estilo ainda. O que fica pronto é a metade difícil: estar dentro do
processo, com a árvore na mão, sem ter feito nada que um antivírus deva sinalizar.
