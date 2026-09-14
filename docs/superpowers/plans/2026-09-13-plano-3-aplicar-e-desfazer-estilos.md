# Plano 3 — Aplicar e desfazer estilos

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `taskbar-styler apply <ThemeId>` estiliza a taskbar viva com qualquer um dos 55 temas e `reset` a restaura, estilizando também elementos que aparecem depois — sem reiniciar o explorer e sem vazar handles.

**Architecture:** O `styler_core` ganha resolução de constantes, expansão de nomes de tipo, a reescrita interina do `WindhawkBlur` e um matcher puro sobre uma visão abstrata de elemento. O TAP ganha C++/WinRT (só os headers pré-gerados do SDK), uma assinatura permanente de `AdviseVisualTreeChange` com o dreno adiado do upstream, um registro de elementos por handle validado por `weak_ref`, e um motor de estilo que resolve nome de propriedade em `DependencyProperty` carregando um `<Style>` de um `<Setter>` via `XamlReader`, guarda o original por `ReadLocalValue` e restaura por `SetValue`/`ClearValue`. O CLI escreve `%APPDATA%\TaskbarStyler\config.json` e sinaliza um Event nomeado; o TAP reaplica em cada thread de host XAML.

**Tech Stack:** C++20 (MSVC), C++/WinRT do Windows SDK 10.0.26100.0 (`Windows.UI.Xaml`), COM registration-free, CMake + Ninja, doctest, nlohmann/json.

**Spec:** `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md` — §4.2 (fronteira entre processos), §4.3 (divisão do C++), §5 (modelo de temas), §6.1/6.4 (múltiplas superfícies, desligar), §7.1 (regra zero), §7.2 (handles), §7.6 (falhar fechado), §8.2 (smoke), §11 (critérios 1, 4, 5).

**Herança do Plano 2:** `docs/superpowers/plano-2-decisoes.md` (lista no fim) e `docs/superpowers/plano-2-spike-arvore-sem-assinatura.md` (a evidência de API em que a captura se apoia).

## Global Constraints

- **Licença GPL-3.0.** Todo `.h`/`.cpp`/`.py` novo leva `// SPDX-License-Identifier: GPL-3.0-or-later`. Não em `CMakeLists.txt` nem em `*.yml`.
- **Idioma:** identificadores e comentários de código em **inglês**; documentação em **português**.
- **C++20**, MSVC do Visual Studio 2026 Community, Windows SDK 10.0.26100.0. **x64 apenas.**
- **Nenhuma API de injeção, nenhum patch de código.** Proibidos: `VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread`, `SetThreadContext`, MinHook, Detours. `GetProcAddress` em `ntdll.dll` para **consultar** `RtlQueryFeatureConfiguration` (Task 7) é leitura, não injeção — o grep do CI não o cobre e não deve passar a cobrir.
- **Zero acesso à rede em runtime.**
- **Nada de diálogo modal a partir do TAP.**
- **`styler_core` permanece livre de Windows.** Este plano o **modifica** (adições puras), mas nada nele inclui `<windows.h>` ou `winrt/`.
- **A fronteira COM mora em `tap_boundary.cpp`** (`DllGetClassObject`, `DllCanUnloadNow`, `SetSite`, `GetSite`); os callbacks de árvore moram junto do objeto que os implementa (spec §7.1). Toda entrada chamável pelo XAML é catch-all e devolve `S_OK`.
- **C++/WinRT só dos headers pré-gerados do SDK** (`Include\10.0.26100.0\cppwinrt\winrt\*.h`), linkando `WindowsApp.lib`, com `WINRT_LEAN_AND_MEAN`. **Sem NuGet, sem passo de `cppwinrt.exe` no build, sem projeção de `Microsoft.UI.Xaml`** (não vem no SDK — é por isso que a reciclagem do `ItemsRepeater` fica para o Plano 3b).
- **Toda chamada WinRT dentro do TAP fica dentro de `try { } catch (winrt::hresult_error const&) { } catch (...) { }`.** Uma exceção C++/WinRT escapando de um callback do XAML é um crash dentro do explorer.
- **Estado por thread é `thread_local`.** Registro de elementos, estado de customização e fila de liberação nunca cruzam threads (spec §6.1; upstream `vendor/upstream/windows-11-taskbar-styler.wh.cpp:11834`, `:18281`).
- **O callback da assinatura permanente nunca libera handle** (spec §7.2, nota Plano 2 → Plano 3). Liberação só pela fila + dreno no dispatcher da thread.
- **Falhar fechado por regra, nunca pela metade por tema** (spec §7.6): uma regra que não aplica é logada em `Error` e as demais seguem.

## Fatos verificados antes de escrever este plano

Cada afirmação técnica abaixo foi conferida contra o SDK, o corpus ou um probe compilado — não contra memória. Quem executar não precisa re-derivar; quem duvidar tem onde olhar.

| Fato | Como foi verificado |
|---|---|
| O SDK 10.0.26100.0 traz os headers C++/WinRT pré-gerados (345 arquivos em `cppwinrt\winrt\`, inclusive `windows.ui.xaml.*.h`), `cppwinrt.exe` e `WindowsApp.lib`. | `ls` no SDK, 2026-09-13. |
| Um TU com `XamlReader::Load`, `Style.Setters().GetAt(0).as<Setter>()`, `ReadLocalValue`, `SetValue`, `DispatcherQueue::GetForCurrentThread`, `VisualStateManager::GetVisualStateGroups` e `VisualTreeHelper::GetParent` compila com `/std:c++20 /EHsc /permissive- /W4 /utf-8 /DWINRT_LEAN_AND_MEAN` sob o VsDevCmd em **19 s** (obj de 941 KB). **`<winrt/Windows.Foundation.Collections.h>` é obrigatório** — sem ele `GetAt` dá C3779. | Probe `winrt_probe2.cpp`, 2026-09-13. |
| O upstream **não** usa `IVisualTreeService::SetProperty`/`ClearProperty`: aplica via WinRT. Nome de propriedade vira `DependencyProperty` carregando um `<ResourceDictionary><Style TargetType="…"><Setter Property="X" …/></Style></ResourceDictionary>` por `XamlReader::Load` e lendo `setter.Property()`/`setter.Value()`. | `vendor:15394-15440` (`GetStyleFromXamlSetters`), `:15489-15565`. |
| Tipos custom (`Taskbar.TaskbarFrame`) resolvem por `xmlns:windhawkstyler="using:<namespace>"` + `TargetType="windhawkstyler:<Type>"`; quando o XAML não conhece o tipo (erro `0x802B000A`), o upstream repete com um tipo de fallback. | `vendor:15394-15466`. |
| Original é lido com `ReadLocalValue`; quando devolve `BindingExpression(Base)`, usa-se `GetAnimationBaseValue` (não dá para `SetValue` de volta um binding). Restaurar é `SetValue(original)` ou `ClearValue` quando o original era `UnsetValue`. | `vendor:12377-12400`, `SetOrClearValue`. |
| `Rectangle#BackgroundFill.Fill` setado antes do `OnApplyTemplate` do `TaskbarBackground` **pode crashar**; o upstream adia esse `SetValue` para `Dispatcher().TryRunAsync(High)`. | `vendor:14940-14990`, issue linkada no comentário. |
| O lote inicial de `AdviseVisualTreeChange` chega **síncrono dentro da chamada**, na thread chamadora; liberar handle **de dentro** do callback destrói o elemento no meio da varredura; o upstream enfileira (`thread_local`) e drena num `DispatcherQueueTimer` de disparo único após 200 ms de silêncio, mantendo presos os handles de elementos que ainda têm estado. | Spike do Plano 2; `vendor:18281-18430`. |
| Handles são endereços e podem ser reusados por elemento novo: o upstream valida o registro com `weak_ref` (`entry.element.get() == element`). | `vendor:11841-11900`. |
| Constantes: o upstream resolve por **prefixo, nome mais longo primeiro** (`LoadStyleConstants` ordena por tamanho) e um `$` sem correspondência passa como literal; valores de constante podem referenciar constantes **declaradas antes**. No corpus: 30 pares de nomes que são prefixo um do outro em 14 temas dependem disso; as 5 referências aninhadas apontam para constantes anteriores. | `vendor:18536-18600`; medição no corpus, 2026-09-13. |
| Corpus (55 temas, 2396 regras, 7170 estilos): 2096 `:=` (XAML), 842 estilos com `@VisualState` (38 temas), 236 alvos com `@VSG`, 206 alvos com `[Prop=val]`, 41 com `[n]`, 11 `:root`, 1 `*`, 624 alvos multi-cadeia, 1683 com `$`; 29 capturas `=>` e 108 `{{…}}` (14 temas); `WindhawkBlur` em 32 temas; `resourceVariables` em 3 temas (24 entradas); 1 `osFeatureVariant`. | Medição no corpus, 2026-09-13. |
| Nomes de tipo curtos no JSON (`Grid`) são expandidos pelo motor: sem `.`/`:` → `Windows.UI.Xaml.Controls.` + nome, exceto `Rectangle` → `Windows.UI.Xaml.Shapes.Rectangle`; prefixos `taskbar:`, `systemtray:`, `udk:`, `muxc:`. | `vendor:18795-18815`. |
| `IXamlDiagnostics::GetInitializationData(BSTR*)` devolve a string passada como 6º argumento de `InitializeXamlDiagnosticsEx`; hoje o CLI passa `nullptr`. | `xamlOM.h:732`; `src/cli/loader.cpp:75`. |

## Fora deste plano — Plano 3b

Ficam explicitamente para o próximo plano, cada um com a referência do upstream para não redescobrir:

- **`WindhawkBlur` real** — `XamlCompositionBrushBase` com `IGraphicsEffectD2D1Interop` declarado à mão (sem Win2D), ~1000 linhas (`vendor:12505-13600`). Neste plano, `<WindhawkBlur …/>` é reescrito para `<AcrylicBrush …/>` mantendo `TintColor`, `TintOpacity`, `TintLuminosityOpacity` e `FallbackColor` (nomes idênticos no `AcrylicBrush`) e descartando `BlurAmount`, `TintSaturation`, `NoiseOpacity`, `NoiseDensity`. É uma aproximação declarada, contada no log.
- **Capturas `Prop=>Var` e valores dinâmicos `{{Var}}`** — 137 estilos em 14 temas (`Blob`, `Pills`, `LiquidGlass2`, …). Neste plano são **pulados com diagnóstico**, nunca aplicados errado (`vendor:11941-12140`, `SetUpCapturesForElement`).
- **Reciclagem do `ItemsRepeater`** — o pool de reciclagem não reporta Remove/Add; o upstream ouve `ElementClearing`/`ElementPrepared` (`vendor:HandleVirtualizingRepeater`). Precisa da projeção `Microsoft.UI.Xaml`, que não está no SDK.
- **Retry de imagem remota** (`TrackIfRemoteImageSource`) e **click-through** — fora do escopo do produto (spec §2).

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `src/core/include/styler/constants.h`, `src/core/constants.cpp` | `ResolveConstants` (aninhadas, ordenadas por tamanho) e `ApplyStyleConstants` |
| `src/core/include/styler/type_name.h`, `src/core/type_name.cpp` | `AdjustTypeName` |
| `src/core/include/styler/blur_rewrite.h`, `src/core/blur_rewrite.cpp` | `RewriteWindhawkBlur` → `AcrylicBrush` (interino) |
| `src/core/include/styler/matcher.h`, `src/core/matcher.cpp` | `ElementView`, `PrepareTheme`, `MatchesChain`, `FindMatchingRules` — puro |
| `src/core/include/styler/config.h`, `src/core/config.cpp` | `Config`, `ParseConfigJson`, `SerializeConfigJson` |
| `src/tap/winrt_common.h` | Os includes C++/WinRT (um lugar só) e os aliases de namespace |
| `src/tap/ipc.h` | Nome do Event de recarga e caminho relativo do config — compartilhado com o CLI |
| `src/tap/element_registry.h/.cpp` | `thread_local` handle → `{weak_ref, ElementId}`; validação por `weak_ref` |
| `src/tap/release_policy.h/.cpp` | A parte pura da fila: dedupe + filtro "ainda tem estado" (lib estático, testável) |
| `src/tap/release_queue.h/.cpp` | `thread_local` fila + `DispatcherQueueTimer` de dreno |
| `src/tap/change_subscription.h/.cpp` | O callback permanente `IVisualTreeServiceCallback2` e `Start/StopSubscription` |
| `src/tap/property_setter.h/.cpp` | Nome → `DependencyProperty` via `<Style>`, `ReadLocalValueWithWorkaround`, `SetOrClearValue` |
| `src/tap/style_engine.h/.cpp` | Estado por elemento, aplicar/restaurar, estados visuais, `ElementHasState` |
| `src/tap/resource_variables.h/.cpp` | Mescla de `resourceVariables` no `ResourceDictionary` da thread |
| `src/tap/theme_session.h/.cpp` | Lê config + tema, prepara, `SetTheme`; espera o Event de recarga e reaplica por thread |
| `src/tap/tap_boundary.cpp` | `SetSite` chama `StartThemeSession` e `StartSubscription` depois do export |
| `src/cli/loader.h/.cpp`, `src/cli/main.cpp` | `load` passa a pasta `themes`; `apply`, `reset`, `list`, `status` |
| `tests/core/test_constants.cpp`, `test_type_name.cpp`, `test_blur_rewrite.cpp`, `test_matcher.cpp`, `test_config.cpp`, `test_corpus_resolution.cpp` | Testes das partes puras |
| `tests/tap/test_release_policy.cpp` | Teste da política de liberação |
| `docs/smoke-test.md`, `README.md`, `.github/workflows/ci.yml` | Checklist de aplicar/desfazer, uso, verificações |

**Onde mora cada estado.** O tema preparado (`styler::ResolvedTheme`) é imutável e global (`std::atomic<std::shared_ptr<const ResolvedTheme>>`), como `g_session`. Tudo que aponta para elementos XAML é `thread_local`: cada thread de host XAML tem seu registro, seu estado de customização e sua fila. O Event de recarga acorda uma thread de pool que só faz `RunOnWindowThread` para cada host; o trabalho real acontece na thread do host.

---

### Task 1: C++/WinRT no TAP e a pasta de temas como dado de inicialização

Traz a projeção para o build e prova, dentro do explorer, que ela funciona num objeto XAML real — antes de qualquer lógica depender disso. Também faz o CLI passar a pasta `themes` para o TAP, que é como o TAP vai achar os JSONs sem registry e sem caminho fixo.

**Files:**
- Create: `src/tap/winrt_common.h`
- Modify: `src/tap/CMakeLists.txt`, `src/cli/CMakeLists.txt`, `src/tap/tap_boundary.cpp`, `src/tap/visual_tree_watcher.h`, `src/tap/visual_tree_watcher.cpp`, `src/cli/loader.h`, `src/cli/loader.cpp`, `src/cli/main.cpp`, `.github/workflows/ci.yml`
- Test: `tests/tap/test_loader.cpp` (se referenciar `LoadTap`, acompanha a assinatura)

**Interfaces:**
- Consumes: `OpenDiagnostics`, `AcquireSession()`, `DiagnosticsSession::diagnostics()` (Plano 2).
- Produces:
  - `src/tap/winrt_common.h` — o único lugar que inclui `winrt/*.h`.
  - `LoadResult styler::cli::LoadTap(DWORD pid, const std::wstring& tap_path, const std::wstring& init_data)` — `init_data` chega no TAP por `IXamlDiagnostics::GetInitializationData`.
  - `std::wstring styler::cli::ThemesDir()` — `<pasta do exe>\themes`, ou vazio se não existir.
  - `std::wstring styler::tap::InitializationData()` — o que o CLI passou, lido uma vez em `OpenDiagnostics`; vazio se nada foi passado.
  - `wf::IInspectable styler::tap::InspectableFromRaw(::IInspectable*)` — adota a referência que `GetIInspectableFromHandle`/`GetUiLayer` já incrementaram.

- [ ] **Step 1: `winrt_common.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one place that pulls C++/WinRT in. Every other TAP source includes
// this, never winrt/*.h directly, so the include set (and the mandatory
// Windows.Foundation.Collections.h - without it IVector::GetAt fails with
// C3779) is decided once. These headers cost ~15-20 s per translation unit;
// keep the number of TUs that include this small.
#ifndef WINRT_LEAN_AND_MEAN
#define WINRT_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace styler::tap {

namespace wf = winrt::Windows::Foundation;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxm = winrt::Windows::UI::Xaml::Media;

// Wraps a WinRT object obtained from a raw diagnostics out-parameter without
// touching its reference count: GetIInspectableFromHandle / GetUiLayer
// already AddRef'd it, and com_ptr::attach takes that reference over.
inline wf::IInspectable InspectableFromRaw(::IInspectable* raw) {
    winrt::com_ptr<::IInspectable> owned;
    owned.attach(raw);
    return owned.as<wf::IInspectable>();
}

}  // namespace styler::tap
```

- [ ] **Step 2: CMake — linkar `WindowsApp.lib` e tolerar TUs grandes**

Em `src/tap/CMakeLists.txt`, depois de `target_link_libraries(taskbar_styler_tap PRIVATE taskbar_styler_tap_lib)`:

```cmake
# C++/WinRT comes from the Windows SDK's pre-generated headers (no NuGet, no
# cppwinrt.exe step). WindowsApp.lib is the umbrella import library that
# resolves the RoXxx activation entry points those headers call. /bigobj
# because a TU that includes the XAML projection routinely exceeds the
# default 65k-section limit.
target_link_libraries(taskbar_styler_tap PRIVATE WindowsApp.lib)
target_compile_definitions(taskbar_styler_tap PRIVATE WINRT_LEAN_AND_MEAN)
target_compile_options(taskbar_styler_tap PRIVATE /bigobj)
```

- [ ] **Step 3: O CLI passa a pasta de temas**

`src/cli/loader.h` — trocar a declaração de `LoadTap` e somar `ThemesDir`:

```cpp
// Loads the TAP into `pid`. `init_data` is handed to the TAP verbatim through
// IXamlDiagnostics::GetInitializationData - today it carries the absolute
// path of the themes directory. May be empty.
LoadResult LoadTap(DWORD pid, const std::wstring& tap_path,
                   const std::wstring& init_data);

// <directory of this exe>\themes, or empty when that directory does not exist.
std::wstring ThemesDir();
```

`src/cli/loader.cpp` — na chamada (linha ~75), o 6º argumento deixa de ser `nullptr`:

```cpp
        HRESULT hr = ixde(connection, pid, L"", tap_path.c_str(),
                          styler::tap::CLSID_TaskbarStylerTap,
                          init_data.empty() ? nullptr : init_data.c_str());
```

e, ao lado de `TapDllPath()` (mesma técnica de achar a pasta do exe):

```cpp
std::wstring ThemesDir() {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        return L"";
    }
    std::wstring dir(exe);
    size_t slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return L"";
    }
    dir.resize(slash + 1);
    dir += L"themes";
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"";
    }
    return dir;
}
```

`src/cli/main.cpp`, em `CmdLoad()`, antes de `LoadTap`:

```cpp
    std::wstring themes = styler::cli::ThemesDir();
    if (themes.empty()) {
        wprintf(L"aviso: pasta themes nao encontrada ao lado do executavel; "
                L"o TAP carrega, mas nao tera temas para aplicar.\n");
    }
    auto r = styler::cli::LoadTap(pid, tap, themes);
```

E em `src/cli/CMakeLists.txt`, copiar `themes/` para junto do exe a cada build, para o smoke test ter os JSONs onde `ThemesDir()` procura:

```cmake
# The CLI looks for themes next to its own exe (ThemesDir()). Mirror the
# repository's themes/ there after every build so a fresh build tree is
# runnable without a manual copy.
add_custom_command(TARGET taskbar_styler_cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/themes"
          "$<TARGET_FILE_DIR:taskbar_styler_cli>/themes")
```

- [ ] **Step 4: O TAP lê o dado de inicialização e prova a projeção**

Em `src/tap/visual_tree_watcher.h`, declarar:

```cpp
// The initialization string the loader passed to InitializeXamlDiagnosticsEx
// (today: the absolute themes directory), read once by OpenDiagnostics.
// Empty when none was passed. Safe to call from any thread after
// OpenDiagnostics returned.
std::wstring InitializationData();
```

Em `src/tap/visual_tree_watcher.cpp`, no namespace anônimo, ao lado de `g_session` e pelo mesmo motivo (nenhum destrutor de escopo de namespace pode rodar no `DLL_PROCESS_DETACH` — Ruling 11 do Plano 2):

```cpp
// Written once by OpenDiagnostics, read from any thread afterwards.
auto* const g_init_data =
    new std::atomic<std::shared_ptr<const std::wstring>>{
        std::make_shared<const std::wstring>()};
```

Dentro de `OpenDiagnostics`, logo depois do QI de `IXamlDiagnostics` dar certo:

```cpp
    {
        BSTR data = nullptr;
        std::wstring value;
        if (SUCCEEDED(diagnostics->GetInitializationData(&data)) && data) {
            value.assign(data, SysStringLen(data));
            SysFreeString(data);
        }
        g_init_data->store(std::make_shared<const std::wstring>(std::move(value)));
    }
```

E a função pública:

```cpp
std::wstring InitializationData() {
    return *g_init_data->load();
}
```

A prova da projeção vai em `tap_boundary.cpp`, que passa a incluir `<tap/winrt_common.h>` (é o primeiro TU do TAP a pagar os ~20 s). No namespace anônimo:

```cpp
// Proves, once per load, that the C++/WinRT projection works on a real XAML
// object inside explorer: GetUiLayer returns the diagnostics adorner Grid
// (spike: S_OK, detached, zero children). If this line ever stops logging
// "Windows.UI.Xaml.Controls.Grid", every later task's assumption is gone.
void ProbeWinRt(const std::shared_ptr<DiagnosticsSession>& session) {
    try {
        ::IInspectable* raw = nullptr;
        HRESULT hr = session->diagnostics()->GetUiLayer(&raw);
        if (FAILED(hr) || !raw) {
            STYLER_LOG(LogLevel::Error, L"GetUiLayer failed 0x%08X",
                       static_cast<unsigned>(hr));
            return;
        }
        auto layer = InspectableFromRaw(raw);
        STYLER_LOG(LogLevel::Info, L"winrt ok: %s",
                   winrt::get_class_name(layer).c_str());
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"ProbeWinRt hresult 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"ProbeWinRt threw");
    }
}
```

No `SetSite`, depois do bloco do `ExportTreeToFile`:

```cpp
                STYLER_LOG(LogLevel::Info, L"init data: %s",
                           InitializationData().c_str());
                if (auto session = AcquireSession()) {
                    ProbeWinRt(session);
                }
```

- [ ] **Step 5: CI — garantir que a projeção vem só do SDK**

Em `.github/workflows/ci.yml`, um passo novo depois de "Nenhuma API de injecao no codigo":

```yaml
      - name: C++/WinRT so do SDK, sem projecao WinUI
        shell: bash
        run: |
          ! find . -name packages.config -not -path './build/*' | grep .
          ! grep -rn "winrt/Microsoft.UI" src/
          # Exactly one include site for the projection: winrt_common.h.
          test "$(grep -rln '#include <winrt/' src/ | wc -l)" -eq 1
```

- [ ] **Step 6: Build, testes e smoke**

Reinicie o Explorer (a DLL fica travada enquanto carregada), então:

```
cmake --build build
ctest --test-dir build --output-on-failure
build\src\cli\taskbar-styler.exe load
```

Esperado no log: `init data: <caminho>\build\src\cli\themes` e `winrt ok: Windows.UI.Xaml.Controls.Grid`. Anote o tempo de build do `tap_boundary.cpp` no relatório — é a linha de base do custo dos headers.

- [ ] **Step 7: Commit**

```bash
git add src/tap/winrt_common.h src/tap/CMakeLists.txt src/tap/tap_boundary.cpp src/tap/visual_tree_watcher.h src/tap/visual_tree_watcher.cpp src/cli/ .github/workflows/ci.yml
git commit -m "feat(tap): C++/WinRT do SDK no TAP e pasta de temas como dado de inicializacao"
```

---

### Task 2: Core — constantes, nomes de tipo e a reescrita do `WindhawkBlur`

Três funções string → string, todas puras, todas com o corpus como prova. A resolução de constantes é onde a fidelidade se decide: 14 temas dependem de "nome mais longo primeiro".

**Files:**
- Create: `src/core/include/styler/constants.h`, `src/core/constants.cpp`, `src/core/include/styler/type_name.h`, `src/core/type_name.cpp`, `src/core/include/styler/blur_rewrite.h`, `src/core/blur_rewrite.cpp`
- Create: `tests/core/test_constants.cpp`, `tests/core/test_type_name.cpp`, `tests/core/test_blur_rewrite.cpp`, `tests/core/test_corpus_resolution.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `styler::Theme::constants` (`std::map<std::wstring, std::wstring>`), `styler::LoadThemeFromFile`, `styler::Utf8ToWide`.
- Produces:
  - `using styler::ResolvedConstants = std::vector<std::pair<std::wstring, std::wstring>>;` — ordenadas por tamanho do nome, decrescente.
  - `ResolvedConstants styler::ResolveConstants(const std::map<std::wstring, std::wstring>& constants);`
  - `std::wstring styler::ApplyStyleConstants(std::wstring_view text, const ResolvedConstants& constants);`
  - `std::wstring styler::AdjustTypeName(std::wstring_view type);`
  - `std::wstring styler::RewriteWindhawkBlur(std::wstring_view value, bool* rewritten);`

- [ ] **Step 1: Testes das constantes**

`tests/core/test_constants.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <string>

#include <styler/constants.h>

using styler::ApplyStyleConstants;
using styler::ResolveConstants;

TEST_CASE("substitutes a constant anywhere in the value") {
    auto c = ResolveConstants({{L"Bg", L"Red"}});
    CHECK(ApplyStyleConstants(L"Fill:=$Bg", c) == L"Fill:=Red");
    CHECK(ApplyStyleConstants(L"<Brush Color=\"$Bg\"/>", c) ==
          L"<Brush Color=\"Red\"/>");
}

TEST_CASE("an unmatched dollar passes through as a literal") {
    auto c = ResolveConstants({{L"Bg", L"Red"}});
    CHECK(ApplyStyleConstants(L"Text=$Nope and $", c) == L"Text=$Nope and $");
}

TEST_CASE("the longest matching name wins regardless of map order") {
    // Aeris declares themeColor before themeColorOpacity and references the
    // long one; upstream sorts by length so the long name wins.
    auto c = ResolveConstants(
        {{L"themeColor", L"#FF0000"}, {L"themeColorOpacity", L"0.5"}});
    CHECK(ApplyStyleConstants(L"$themeColorOpacity", c) == L"0.5");
    CHECK(ApplyStyleConstants(L"$themeColor", c) == L"#FF0000");
    CHECK(ApplyStyleConstants(L"$themeColorX", c) == L"#FF0000X");
}

TEST_CASE("a constant may reference another constant") {
    // WindowGlass: Background=$Glass, Glass=<WindhawkBlur .../>.
    auto c = ResolveConstants(
        {{L"Background", L"$Glass"}, {L"Glass", L"<AcrylicBrush/>"}});
    CHECK(ApplyStyleConstants(L"Fill:=$Background", c) ==
          L"Fill:=<AcrylicBrush/>");
}

TEST_CASE("a self-referencing constant does not loop forever") {
    auto c = ResolveConstants({{L"A", L"$A"}});
    CHECK(ApplyStyleConstants(L"$A", c) == L"$A");
}

TEST_CASE("resolved constants are sorted longest name first") {
    auto c = ResolveConstants({{L"a", L"1"}, {L"abc", L"3"}, {L"ab", L"2"}});
    REQUIRE(c.size() == 3);
    CHECK(c[0].first == L"abc");
    CHECK(c[1].first == L"ab");
    CHECK(c[2].first == L"a");
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `cmake --build build && ctest --test-dir build -R core --output-on-failure`
Expected: falha de compilação — `styler/constants.h` não existe.

- [ ] **Step 3: `constants.h` / `constants.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace styler {

// Constants ready for substitution: nested `$Name` references already
// expanded, sorted by name length descending so the longest name that
// prefixes the text after a `$` wins - exactly upstream's LoadStyleConstants
// order (vendor/upstream/windows-11-taskbar-styler.wh.cpp:18580). 14 shipped
// themes have constant names that prefix one another (Aeris themeColor /
// themeColorOpacity, Matter overlay / overlay2, ...) and reference the long
// one; any other order would splice the short value into the long name.
using ResolvedConstants = std::vector<std::pair<std::wstring, std::wstring>>;

ResolvedConstants ResolveConstants(
    const std::map<std::wstring, std::wstring>& constants);

// Replaces every `$Name` whose Name is a prefix-match of a resolved constant
// with its value, anywhere in `text`. A `$` matching no constant stays as a
// literal - three shipped themes rely on that (spec section 7.6), so this is
// deliberately not an error.
std::wstring ApplyStyleConstants(std::wstring_view text,
                                 const ResolvedConstants& constants);

}  // namespace styler
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/constants.h>

#include <algorithm>

namespace styler {

std::wstring ApplyStyleConstants(std::wstring_view text,
                                 const ResolvedConstants& constants) {
    std::wstring result;
    size_t last = 0;
    size_t pos;
    while ((pos = text.find(L'$', last)) != std::wstring_view::npos) {
        result.append(text, last, pos - last);
        const std::pair<std::wstring, std::wstring>* hit = nullptr;
        for (const auto& c : constants) {
            if (text.substr(pos + 1, c.first.size()) == c.first) {
                hit = &c;  // First hit is the longest: `constants` is sorted.
                break;
            }
        }
        if (hit) {
            result += hit->second;
            last = pos + 1 + hit->first.size();
        } else {
            result += L'$';
            last = pos + 1;
        }
    }
    result.append(text.substr(last));
    return result;
}

ResolvedConstants ResolveConstants(
    const std::map<std::wstring, std::wstring>& constants) {
    ResolvedConstants resolved(constants.begin(), constants.end());
    std::stable_sort(resolved.begin(), resolved.end(),
                     [](const auto& a, const auto& b) {
                         return a.first.size() > b.first.size();
                     });

    // Upstream expands a constant's value against the constants declared
    // before it, in declaration order. The map has lost that order, so
    // iterate to a fixed point instead: the corpus only ever references
    // earlier declarations (measured, test_corpus_resolution.cpp), where
    // both give the same answer. The pass cap makes a self-reference like
    // A=$A terminate instead of growing forever.
    constexpr int kMaxPasses = 8;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        bool changed = false;
        for (auto& [name, value] : resolved) {
            if (value.find(L'$') == std::wstring::npos) {
                continue;
            }
            std::wstring expanded = ApplyStyleConstants(value, resolved);
            if (expanded != value) {
                value = std::move(expanded);
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return resolved;
}

}  // namespace styler
```

Atenção ao teste `a self-referencing constant does not loop forever`: `A=$A` expande para `$A` de novo (o próprio valor), então `changed` fica `false` na primeira passada e sai. Se a implementação divergir disso, o cap de 8 passadas é a segunda rede.

- [ ] **Step 4: Testes e implementação de `AdjustTypeName`**

`tests/core/test_type_name.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/type_name.h>

using styler::AdjustTypeName;

TEST_CASE("bare control names get the Controls namespace") {
    CHECK(AdjustTypeName(L"Grid") == L"Windows.UI.Xaml.Controls.Grid");
    CHECK(AdjustTypeName(L"Border") == L"Windows.UI.Xaml.Controls.Border");
}

TEST_CASE("Rectangle is a Shape, not a Control") {
    CHECK(AdjustTypeName(L"Rectangle") == L"Windows.UI.Xaml.Shapes.Rectangle");
}

TEST_CASE("dotted names pass through unchanged") {
    CHECK(AdjustTypeName(L"Taskbar.TaskbarFrame") == L"Taskbar.TaskbarFrame");
    CHECK(AdjustTypeName(L"Windows.UI.Xaml.Controls.Grid") ==
          L"Windows.UI.Xaml.Controls.Grid");
}

TEST_CASE("xml-style prefixes expand to their namespaces") {
    CHECK(AdjustTypeName(L"taskbar:TaskListButton") == L"Taskbar.TaskListButton");
    CHECK(AdjustTypeName(L"systemtray:Foo") == L"SystemTray.Foo");
    CHECK(AdjustTypeName(L"udk:Bar") == L"WindowsUdk.UI.Shell.Bar");
    CHECK(AdjustTypeName(L"muxc:ItemsRepeater") ==
          L"Microsoft.UI.Xaml.Controls.ItemsRepeater");
}

TEST_CASE("an unknown prefix passes through unchanged") {
    CHECK(AdjustTypeName(L"foo:Bar") == L"foo:Bar");
}
```

`src/core/include/styler/type_name.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// Expands the short type names theme JSON uses (`Grid`) into the runtime
// class names the visual tree reports (`Windows.UI.Xaml.Controls.Grid`),
// mirroring upstream's AdjustTypeName
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:18795). The export in
// visual-tree.txt already shows full names; this is what lets a selector
// written against either form match.
std::wstring AdjustTypeName(std::wstring_view type);

}  // namespace styler
```

`src/core/type_name.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/type_name.h>

#include <utility>
#include <vector>

namespace styler {

std::wstring AdjustTypeName(std::wstring_view type) {
    if (type.find_first_of(L".:") == std::wstring_view::npos) {
        if (type == L"Rectangle") {
            return L"Windows.UI.Xaml.Shapes.Rectangle";
        }
        return L"Windows.UI.Xaml.Controls." + std::wstring(type);
    }

    static const std::vector<std::pair<std::wstring_view, std::wstring_view>>
        kPrefixes = {
            {L"taskbar:", L"Taskbar."},
            {L"systemtray:", L"SystemTray."},
            {L"udk:", L"WindowsUdk.UI.Shell."},
            {L"muxc:", L"Microsoft.UI.Xaml.Controls."},
        };
    for (const auto& [prefix, ns] : kPrefixes) {
        if (type.starts_with(prefix)) {
            std::wstring out(ns);
            out += type.substr(prefix.size());
            return out;
        }
    }
    return std::wstring(type);
}

}  // namespace styler
```

- [ ] **Step 5: Testes e implementação de `RewriteWindhawkBlur`**

`tests/core/test_blur_rewrite.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/blur_rewrite.h>

using styler::RewriteWindhawkBlur;

TEST_CASE("keeps the attributes AcrylicBrush understands, drops the rest") {
    bool rewritten = false;
    auto out = RewriteWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\" "
        L"TintOpacity=\"0.7\" TintLuminosityOpacity=\"0.3\" "
        L"TintSaturation=\"1.2\" NoiseOpacity=\"0.1\" NoiseDensity=\"0.8\" "
        L"FallbackColor=\"#FF202020\"/>",
        &rewritten);
    CHECK(rewritten);
    CHECK(out ==
          L"<AcrylicBrush TintColor=\"#25323232\" TintOpacity=\"0.7\" "
          L"TintLuminosityOpacity=\"0.3\" FallbackColor=\"#FF202020\"/>");
}

TEST_CASE("theme resource references survive") {
    bool rewritten = false;
    auto out = RewriteWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"5\" "
        L"TintColor=\"{ThemeResource SystemChromeMediumColor}\" />",
        &rewritten);
    CHECK(rewritten);
    CHECK(out ==
          L"<AcrylicBrush TintColor=\"{ThemeResource SystemChromeMediumColor}\"/>");
}

TEST_CASE("Blur is accepted as a synonym") {
    bool rewritten = false;
    CHECK(RewriteWindhawkBlur(L"<Blur BlurAmount=\"3\"/>", &rewritten) ==
          L"<AcrylicBrush/>");
    CHECK(rewritten);
}

TEST_CASE("anything else passes through untouched") {
    bool rewritten = true;
    CHECK(RewriteWindhawkBlur(L"<SolidColorBrush Color=\"Red\"/>", &rewritten) ==
          L"<SolidColorBrush Color=\"Red\"/>");
    CHECK_FALSE(rewritten);
    CHECK(RewriteWindhawkBlur(L"Transparent", &rewritten) == L"Transparent");
    CHECK_FALSE(rewritten);
}

TEST_CASE("leading whitespace is tolerated") {
    bool rewritten = false;
    CHECK(RewriteWindhawkBlur(
              L"  <WindhawkBlur BlurAmount=\"8\" TintColor=\"#761E1E1E\"/>",
              &rewritten) == L"<AcrylicBrush TintColor=\"#761E1E1E\"/>");
    CHECK(rewritten);
}
```

`src/core/include/styler/blur_rewrite.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// ponytail: interim approximation. `<WindhawkBlur .../>` is upstream's own
// composition brush (blur + tint + noise), ~1000 lines that Plano 3b ports.
// Until then it is rewritten to a stock AcrylicBrush keeping the attributes
// the two share by name - TintColor, TintOpacity, TintLuminosityOpacity,
// FallbackColor - and dropping BlurAmount, TintSaturation, NoiseOpacity and
// NoiseDensity. `<Blur .../>` is accepted as a synonym (spec section 5.3).
// `*rewritten` reports whether anything changed so the caller can count how
// many styles run on the approximation. Any other value passes through.
std::wstring RewriteWindhawkBlur(std::wstring_view value, bool* rewritten);

}  // namespace styler
```

`src/core/blur_rewrite.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/blur_rewrite.h>

#include <array>

namespace styler {
namespace {

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) {
        s.remove_suffix(1);
    }
    return s;
}

// Returns the `Name="..."` attribute text (including the quotes) or empty.
std::wstring_view FindAttribute(std::wstring_view body,
                                std::wstring_view name) {
    size_t pos = 0;
    while ((pos = body.find(name, pos)) != std::wstring_view::npos) {
        bool at_start =
            pos == 0 || body[pos - 1] == L' ' || body[pos - 1] == L'\t';
        size_t eq = pos + name.size();
        if (at_start && eq + 1 < body.size() && body[eq] == L'=' &&
            body[eq + 1] == L'"') {
            size_t close = body.find(L'"', eq + 2);
            if (close == std::wstring_view::npos) {
                return {};
            }
            return body.substr(pos, close + 1 - pos);
        }
        pos = eq;
    }
    return {};
}

}  // namespace

std::wstring RewriteWindhawkBlur(std::wstring_view value, bool* rewritten) {
    *rewritten = false;
    std::wstring_view s = Trim(value);
    std::wstring_view body;
    bool matched = false;
    for (std::wstring_view tag : {L"<WindhawkBlur", L"<Blur"}) {
        if (s.starts_with(tag) && s.size() > tag.size() &&
            (s[tag.size()] == L' ' || s[tag.size()] == L'/' ||
             s[tag.size()] == L'>')) {
            body = s.substr(tag.size());
            matched = true;
            break;
        }
    }
    if (!matched) {
        return std::wstring(value);
    }
    if (body.ends_with(L"/>")) {
        body.remove_suffix(2);
    } else if (body.ends_with(L">")) {
        body.remove_suffix(1);
    }

    static constexpr std::array<std::wstring_view, 4> kKept = {
        L"TintColor", L"TintOpacity", L"TintLuminosityOpacity",
        L"FallbackColor"};

    std::wstring out = L"<AcrylicBrush";
    for (std::wstring_view name : kKept) {
        std::wstring_view attr = FindAttribute(body, name);
        if (!attr.empty()) {
            out += L' ';
            out += attr;
        }
    }
    out += L"/>";
    *rewritten = true;
    return out;
}

}  // namespace styler
```

- [ ] **Step 6: O teste de corpus — a prova**

`tests/core/test_corpus_resolution.cpp`. Usa `nlohmann::ordered_json` para recuperar a **ordem de declaração** do JSON (o conversor a preservou) e computa o algoritmo do upstream de forma independente, comparando com o nosso tema a tema:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <styler/constants.h>
#include <styler/theme_loader.h>
#include <styler/utf.h>

namespace {

// Upstream's algorithm, written independently of ResolveConstants: expand
// each constant against the ones declared BEFORE it, in declaration order,
// then keep the list sorted longest name first (vendor:18570-18600).
styler::ResolvedConstants UpstreamOrder(
    const nlohmann::ordered_json& constants) {
    styler::ResolvedConstants out;
    for (auto it = constants.begin(); it != constants.end(); ++it) {
        std::wstring name = styler::Utf8ToWide(it.key());
        std::wstring value =
            styler::Utf8ToWide(it.value().get<std::string>());
        value = styler::ApplyStyleConstants(value, out);  // earlier ones only
        auto pos = std::lower_bound(
            out.begin(), out.end(), name,
            [](const auto& e, const std::wstring& n) {
                return e.first.size() > n.size();
            });
        out.insert(pos, {name, value});
    }
    return out;
}

bool IsTheme(const std::filesystem::directory_entry& e) {
    return e.path().extension() == ".json" &&
           e.path().filename() != "credits.json";
}

}  // namespace

TEST_CASE("every shipped theme resolves constants exactly as upstream would") {
    int themes = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(STYLER_THEMES_DIR)) {
        if (!IsTheme(entry)) {
            continue;
        }
        ++themes;
        std::ifstream in(entry.path());
        auto ordered = nlohmann::ordered_json::parse(in);
        auto theme = styler::LoadThemeFromFile(entry.path());

        auto ours = styler::ResolveConstants(theme.constants);
        auto upstream = UpstreamOrder(
            ordered.value("constants", nlohmann::ordered_json::object()));

        std::map<std::wstring, std::wstring> a(ours.begin(), ours.end());
        std::map<std::wstring, std::wstring> b(upstream.begin(), upstream.end());
        INFO("theme " << entry.path().filename().string());
        CHECK(a == b);

        // And every style text expands identically under both.
        for (const auto& rule : ordered["rules"]) {
            for (const auto& style : rule["styles"]) {
                std::wstring s = styler::Utf8ToWide(style.get<std::string>());
                CHECK(styler::ApplyStyleConstants(s, ours) ==
                      styler::ApplyStyleConstants(s, upstream));
            }
        }
    }
    CHECK(themes == 55);
}

TEST_CASE("only the three known themes keep an unresolved dollar") {
    // Spec section 7.6: Luminosity_variant_Dock, Luminosity_variant_Compact
    // and Fluid reference names no constant defines; upstream lets those
    // through as literals.
    std::set<std::wstring> with_unresolved;
    for (const auto& entry :
         std::filesystem::directory_iterator(STYLER_THEMES_DIR)) {
        if (!IsTheme(entry)) {
            continue;
        }
        auto theme = styler::LoadThemeFromFile(entry.path());
        auto constants = styler::ResolveConstants(theme.constants);
        for (const auto& rule : theme.rules) {
            for (const auto& style : rule.styles) {
                const auto* v = std::get_if<styler::ValueRule>(&style);
                if (v && styler::ApplyStyleConstants(v->value, constants)
                                 .find(L'$') != std::wstring::npos) {
                    with_unresolved.insert(theme.id);
                }
            }
        }
    }
    CHECK(with_unresolved ==
          std::set<std::wstring>{L"Luminosity_variant_Dock",
                                 L"Luminosity_variant_Compact", L"Fluid"});
}
```

Se o segundo teste reprovar por o conjunto ser diferente, **não ajuste o conjunto até entender por quê**: ele foi medido no Plano 1 e está na spec. Reporte a diferença.

`tests/core/CMakeLists.txt` ganha os quatro testes e `nlohmann_json::nlohmann_json` em `target_link_libraries` (o core linka `PRIVATE`, então o teste precisa linkar por conta própria). `src/core/CMakeLists.txt` ganha `constants.cpp`, `type_name.cpp`, `blur_rewrite.cpp`.

- [ ] **Step 7: Rodar e ver passar**

Run: `cmake --build build && ctest --test-dir build -R core --output-on-failure`
Expected: PASS, incluindo o corpus (55 temas).

- [ ] **Step 8: Commit**

```bash
git add src/core tests/core
git commit -m "feat(core): resolucao de constantes, nomes de tipo e reescrita interina do WindhawkBlur"
```

---

### Task 3: Core — o matcher puro e a preparação do tema

Toda a lógica de "esta regra casa com este elemento?" fica no core, sem XAML, sobre uma interface abstrata `ElementView` que o TAP adapta. A preparação do tema aplica constantes, expande tipos, reescreve o blur, descarta regras mortas e **pula com diagnóstico** o que este plano não suporta (capturas, valores dinâmicos).

**Files:**
- Create: `src/core/include/styler/matcher.h`, `src/core/matcher.cpp`
- Create: `tests/core/test_matcher.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `styler::Theme`, `styler::ElementMatcher`, `styler::ValueRule`/`CaptureRule` (Plano 1); `ResolveConstants`, `ApplyStyleConstants`, `AdjustTypeName`, `RewriteWindhawkBlur` (Task 2).
- Produces (tudo em `styler/matcher.h`, namespace `styler`):
  - `struct PreparedStyle { std::wstring property, visual_state, value; bool is_xaml; }`
  - `struct PreparedRule { std::vector<std::vector<ElementMatcher>> chains; std::vector<PreparedStyle> styles; size_t source_index; }`
  - `struct ResolvedTheme { std::wstring id; std::vector<PreparedRule> rules; std::map<std::wstring, std::wstring> resource_variables; std::vector<std::wstring> diagnostics; int skipped_captures, skipped_dynamic, blur_approximations; }`
  - `ResolvedTheme PrepareTheme(const Theme& theme);`
  - `class ElementView` (interface abstrata) com `TypeName()`, `ReportedTypeName()`, `Name()`, `Parent()`, `IndexInParent()`, `PropertyEquals(property, expected)`.
  - `struct VisualStateGroupRef { std::wstring name; int ancestor_depth; }` — `0` = o próprio elemento, `1` = pai, …
  - `struct RuleMatch { const PreparedRule* rule; std::optional<VisualStateGroupRef> vsg; }`
  - `bool MatchesChain(const ElementView& leaf, const std::vector<ElementMatcher>& chain, std::optional<VisualStateGroupRef>* vsg);`
  - `std::vector<RuleMatch> FindMatchingRules(const ResolvedTheme& theme, const ElementView& leaf);` — **na ordem inversa das regras** (a última regra do tema vem primeiro), porque é assim que o upstream decide precedência: a última regra que casa vence a propriedade (`vendor:15936`, `rbegin`).

**Semântica que o TAP precisa honrar ao adaptar `ElementView`** (a Task 5 implementa):
- `TypeName()` é `winrt::get_class_name(element)`; `ReportedTypeName()` é o `Type` que o diagnóstico reportou (o upstream aceita qualquer um dos dois **só na folha** — para ancestrais passa `nullptr` como fallback, `vendor:15903`, `:15908`).
- `IndexInParent()` é a posição 0-based entre os filhos de `VisualTreeHelper::GetParent`, ou `-1` sem pai.
- `PropertyEquals(prop, expected)` faz o que o upstream faz em `TestElementMatcher` (`vendor:15884-15912`): lê o valor local, materializa `expected` pela mesma via de `<Setter>`, desembala os dois (`IPropertyValue`, enums como `int32`) e compara; `nullopt` quando não dá para comparar — o matcher trata `nullopt` como "não casa".

- [ ] **Step 1: Testes do matcher**

`tests/core/test_matcher.cpp`. A árvore falsa implementa `ElementView` com comparação de propriedade por igualdade de string:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <styler/matcher.h>
#include <styler/selector.h>
#include <styler/theme.h>

using namespace styler;

namespace {

struct Node {
    std::wstring type;
    std::wstring name;
    std::wstring reported;  // what diagnostics reported; empty = same as type
    std::map<std::wstring, std::wstring> props;
    std::vector<std::shared_ptr<Node>> children;
    Node* parent = nullptr;

    std::shared_ptr<Node> Add(std::wstring t, std::wstring n = L"") {
        auto c = std::make_shared<Node>();
        c->type = std::move(t);
        c->name = std::move(n);
        c->parent = this;
        children.push_back(c);
        return c;
    }
};

class NodeView : public ElementView {
public:
    explicit NodeView(const Node* n) : n_(n) {}
    std::wstring TypeName() const override { return n_->type; }
    std::wstring ReportedTypeName() const override {
        return n_->reported.empty() ? n_->type : n_->reported;
    }
    std::wstring Name() const override { return n_->name; }
    std::unique_ptr<ElementView> Parent() const override {
        return n_->parent ? std::make_unique<NodeView>(n_->parent) : nullptr;
    }
    int IndexInParent() const override {
        if (!n_->parent) return -1;
        const auto& sib = n_->parent->children;
        for (size_t i = 0; i < sib.size(); ++i) {
            if (sib[i].get() == n_) return static_cast<int>(i);
        }
        return -1;
    }
    std::optional<bool> PropertyEquals(std::wstring_view property,
                                       std::wstring_view expected) const override {
        auto it = n_->props.find(std::wstring(property));
        if (it == n_->props.end()) return std::nullopt;
        return it->second == expected;
    }

private:
    const Node* n_;
};

// Frame > Grid#RootGrid > (Background > Grid > Rectangle#BackgroundFill,
//                          Border#Stroke, Border#Stroke)
struct Tree {
    std::shared_ptr<Node> frame, root, bg, grid, fill, b1, b2;
    Tree() {
        frame = std::make_shared<Node>();
        frame->type = L"Taskbar.TaskbarFrame";
        frame->name = L"TaskbarFrame";
        root = frame->Add(L"Windows.UI.Xaml.Controls.Grid", L"RootGrid");
        bg = root->Add(L"Taskbar.TaskbarBackground", L"BackgroundControl");
        grid = bg->Add(L"Windows.UI.Xaml.Controls.Grid");
        fill = grid->Add(L"Windows.UI.Xaml.Shapes.Rectangle", L"BackgroundFill");
        b1 = root->Add(L"Windows.UI.Xaml.Controls.Border", L"Stroke");
        b2 = root->Add(L"Windows.UI.Xaml.Controls.Border", L"Stroke");
        fill->props[L"Visibility"] = L"Visible";
    }
};

std::vector<ElementMatcher> Chain(std::wstring_view s) {
    return ParseSelector(s);
}

}  // namespace

TEST_CASE("a bare type matches by runtime class name") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.fill.get()),
                       Chain(L"Windows.UI.Xaml.Shapes.Rectangle"), nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Windows.UI.Xaml.Controls.Grid"), nullptr));
}

TEST_CASE("the leaf may match the reported type, ancestors may not") {
    Tree t;
    t.fill->reported = L"Custom.Rect";
    t.grid->reported = L"Custom.Grid";
    CHECK(MatchesChain(NodeView(t.fill.get()), Chain(L"Custom.Rect"), nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Custom.Grid > Custom.Rect"), nullptr));
}

TEST_CASE("a full ancestor chain matches outermost first") {
    Tree t;
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Taskbar.TaskbarFrame > Windows.UI.Xaml.Controls.Grid#RootGrid > "
              L"Taskbar.TaskbarBackground > Windows.UI.Xaml.Controls.Grid > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Grid#RootGrid > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
}

TEST_CASE("the wildcard skips any number of ancestors and backtracks") {
    Tree t;
    // Grid ... Rectangle: the nearest Grid ancestor is the anonymous one; the
    // wildcard must also be able to reach RootGrid for the #RootGrid name.
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Grid#RootGrid > * > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Taskbar.TaskbarFrame > * > Windows.UI.Xaml.Controls.Grid > "
              L"Windows.UI.Xaml.Shapes.Rectangle"),
        nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Border > * > "
              L"Windows.UI.Xaml.Shapes.Rectangle"),
        nullptr));
}

TEST_CASE(":root asserts the element has no parent") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.root.get()),
                       Chain(L":root > Taskbar.TaskbarFrame > "
                             L"Windows.UI.Xaml.Controls.Grid"),
                       nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.grid.get()),
                             Chain(L":root > Taskbar.TaskbarBackground > "
                                   L"Windows.UI.Xaml.Controls.Grid"),
                             nullptr));
}

TEST_CASE("a one-based index selects among all siblings") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.b2.get()),
                       Chain(L"Windows.UI.Xaml.Controls.Border#Stroke[3]"),
                       nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.b1.get()),
                             Chain(L"Windows.UI.Xaml.Controls.Border#Stroke[3]"),
                             nullptr));
}

TEST_CASE("a property filter delegates the comparison to the view") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.fill.get()),
                       Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Visibility=Visible]"),
                       nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Visibility=Collapsed]"),
        nullptr));
    // Unreadable property: does not match, does not throw.
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Nope=1]"),
                             nullptr));
}

TEST_CASE("a visual state group on the leaf or an ancestor is reported with its depth") {
    Tree t;
    std::optional<VisualStateGroupRef> vsg;
    REQUIRE(MatchesChain(NodeView(t.fill.get()),
                         Chain(L"Windows.UI.Xaml.Shapes.Rectangle@CommonStates"),
                         &vsg));
    REQUIRE(vsg.has_value());
    CHECK(vsg->name == L"CommonStates");
    CHECK(vsg->ancestor_depth == 0);

    vsg.reset();
    REQUIRE(MatchesChain(NodeView(t.fill.get()),
                         Chain(L"Taskbar.TaskbarBackground@CommonStates > "
                               L"Windows.UI.Xaml.Controls.Grid > "
                               L"Windows.UI.Xaml.Shapes.Rectangle"),
                         &vsg));
    REQUIRE(vsg.has_value());
    CHECK(vsg->ancestor_depth == 2);
}

TEST_CASE("PrepareTheme expands types, applies constants, rewrites blur, skips the unsupported") {
    Theme theme;
    theme.id = L"T";
    theme.constants = {{L"Bg", L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>"}};
    theme.resource_variables = {{L"Accent", L"$Bg"}};

    ThemeRule r1;
    r1.target = L"Grid#RootGrid > Rectangle";
    r1.selector = ParseSelectorGroups(r1.target);
    r1.styles = {ParseStyleRule(L"Fill:=$Bg"), ParseStyleRule(L"Visibility=Collapsed"),
                 ParseStyleRule(L"Width=>W"), ParseStyleRule(L"Height={{W}}")};
    ThemeRule dead;
    dead.target = L"Nope";
    dead.dead = true;
    theme.rules = {r1, dead};

    auto prepared = PrepareTheme(theme);
    REQUIRE(prepared.rules.size() == 1);
    const auto& rule = prepared.rules[0];
    CHECK(rule.source_index == 0);
    REQUIRE(rule.chains.size() == 1);
    CHECK(rule.chains[0][0].type == L"Windows.UI.Xaml.Controls.Grid");
    CHECK(rule.chains[0][1].type == L"Windows.UI.Xaml.Shapes.Rectangle");
    REQUIRE(rule.styles.size() == 2);
    CHECK(rule.styles[0].property == L"Fill");
    CHECK(rule.styles[0].is_xaml);
    CHECK(rule.styles[0].value == L"<AcrylicBrush TintColor=\"#25323232\"/>");
    CHECK(rule.styles[1].property == L"Visibility");
    CHECK_FALSE(rule.styles[1].is_xaml);
    CHECK(prepared.skipped_captures == 1);
    CHECK(prepared.skipped_dynamic == 1);
    CHECK(prepared.blur_approximations == 1);
    CHECK(prepared.resource_variables.at(L"Accent") ==
          L"<AcrylicBrush TintColor=\"#25323232\"/>");
    CHECK(prepared.diagnostics.size() == 2);
}

TEST_CASE("FindMatchingRules returns the last matching rule first") {
    Tree t;
    Theme theme;
    theme.id = L"T";
    for (auto target : {L"Rectangle", L"Grid > Rectangle", L"Border"}) {
        ThemeRule r;
        r.target = target;
        r.selector = ParseSelectorGroups(target);
        r.styles = {ParseStyleRule(L"Opacity=1")};
        theme.rules.push_back(r);
    }
    auto prepared = PrepareTheme(theme);
    auto matches = FindMatchingRules(prepared, NodeView(t.fill.get()));
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].rule->source_index == 1);
    CHECK(matches[1].rule->source_index == 0);
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `cmake --build build && ctest --test-dir build -R core --output-on-failure`
Expected: falha de compilação — `styler/matcher.h` não existe.

- [ ] **Step 3: `matcher.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <styler/selector.h>
#include <styler/theme.h>

namespace styler {

// One style after preparation: constants applied, blur rewritten. `value`
// is XAML markup when `is_xaml` (an empty XAML value means "clear the
// property", upstream `Fill:=`); otherwise it is attribute text.
struct PreparedStyle {
    std::wstring property;
    std::wstring visual_state;  // Empty: unconditional.
    std::wstring value;
    bool is_xaml = false;
};

struct PreparedRule {
    std::vector<std::vector<ElementMatcher>> chains;  // Types expanded.
    std::vector<PreparedStyle> styles;
    size_t source_index = 0;  // Index into Theme::rules, for logs.
};

// A theme ready to apply. Immutable once built; shared across threads.
struct ResolvedTheme {
    std::wstring id;
    std::vector<PreparedRule> rules;  // Dead rules dropped, order kept.
    std::map<std::wstring, std::wstring> resource_variables;
    std::vector<std::wstring> diagnostics;  // Theme's own + what was skipped.
    int skipped_captures = 0;     // `Prop=>Var` - Plano 3b.
    int skipped_dynamic = 0;      // `{{Var}}` values - Plano 3b.
    int blur_approximations = 0;  // WindhawkBlur rewritten to AcrylicBrush.
};

ResolvedTheme PrepareTheme(const Theme& theme);

// What the matcher needs to know about one live element. The TAP adapts a
// FrameworkElement to this; tests use a fake tree. Every method is called
// only while the element is alive on its own UI thread.
class ElementView {
public:
    virtual ~ElementView() = default;
    // Runtime class name (winrt::get_class_name).
    virtual std::wstring TypeName() const = 0;
    // The type the diagnostics reported for this element; accepted as an
    // alternative to TypeName() for the LEAF only (upstream passes the
    // fallback for the matched element and nullptr for its ancestors).
    virtual std::wstring ReportedTypeName() const = 0;
    virtual std::wstring Name() const = 0;
    virtual std::unique_ptr<ElementView> Parent() const = 0;  // null at root.
    virtual int IndexInParent() const = 0;  // 0-based; -1 without a parent.
    // Whether the element's local value of `property` equals `expected` as
    // XAML would parse it. nullopt when it cannot be read or compared - the
    // matcher treats that as "does not match".
    virtual std::optional<bool> PropertyEquals(
        std::wstring_view property, std::wstring_view expected) const = 0;
};

// `@Group` found on a matcher in the chain: the group lives on the element
// `ancestor_depth` parents above the leaf (0 = the leaf itself).
struct VisualStateGroupRef {
    std::wstring name;
    int ancestor_depth = 0;
};

struct RuleMatch {
    const PreparedRule* rule = nullptr;
    std::optional<VisualStateGroupRef> vsg;
};

// `chain` is outermost ancestor first, leaf last (ParseSelector order).
// When `vsg` is non-null it receives the last `@Group` encountered while
// walking, mirroring upstream's single visualStateGroup out-param
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:15936-16000).
bool MatchesChain(const ElementView& leaf,
                  const std::vector<ElementMatcher>& chain,
                  std::optional<VisualStateGroupRef>* vsg);

// Every rule whose ANY chain matches `leaf`, last theme rule first: that is
// the precedence upstream applies (it walks rules from rbegin and the first
// rule to claim a property keeps it). Consumers dedupe per property in the
// order returned.
std::vector<RuleMatch> FindMatchingRules(const ResolvedTheme& theme,
                                         const ElementView& leaf);

}  // namespace styler
```

- [ ] **Step 4: `matcher.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/matcher.h>

#include <variant>

#include <styler/blur_rewrite.h>
#include <styler/constants.h>
#include <styler/style_rule.h>
#include <styler/type_name.h>

namespace styler {
namespace {

// TestElementMatcher (vendor:15855-15922) over the abstract view. `depth`
// is how many parents above the leaf `element` sits, for the VSG ref.
bool TestMatcher(const ElementView& element, const ElementMatcher& m,
                 bool allow_reported_type, int depth,
                 std::optional<VisualStateGroupRef>* vsg) {
    if (!m.type.empty()) {
        if (m.type != element.TypeName() &&
            !(allow_reported_type && m.type == element.ReportedTypeName())) {
            return false;
        }
    }
    if (!m.name.empty() && m.name != element.Name()) {
        return false;
    }
    if (m.one_based_index) {
        int index = element.IndexInParent();
        if (index < 0 || index + 1 != m.one_based_index) {
            return false;
        }
    }
    for (const auto& [property, expected] : m.property_filters) {
        std::optional<bool> eq = element.PropertyEquals(property, expected);
        if (!eq.has_value() || !*eq) {
            return false;
        }
    }
    if (m.visual_state_group && vsg) {
        *vsg = VisualStateGroupRef{*m.visual_state_group, depth};
    }
    return true;
}

// `parents` is nearest ancestor first (the chain reversed, leaf removed).
// Recursive so that '*' can backtrack: when a candidate for the wildcard's
// next matcher fails further up, retry with a farther ancestor
// (vendor:15947-15995).
bool MatchParents(const ElementView& iter, int depth,
                  const std::vector<const ElementMatcher*>& parents, size_t mi,
                  std::optional<VisualStateGroupRef>* vsg) {
    if (mi >= parents.size()) {
        return true;
    }
    const ElementMatcher& m = *parents[mi];

    if (m.kind == ElementMatcher::Kind::Root) {
        if (iter.Parent()) {
            return false;
        }
        return MatchParents(iter, depth, parents, mi + 1, vsg);
    }

    if (m.kind == ElementMatcher::Kind::Wildcard) {
        // Fail closed on a '*' that is last or not followed by an Element
        // matcher: upstream validates that at parse time; ours does not yet
        // (Plano 1 deferred it), so the chain simply never matches.
        if (mi + 1 >= parents.size() ||
            parents[mi + 1]->kind != ElementMatcher::Kind::Element) {
            return false;
        }
        const ElementMatcher& next = *parents[mi + 1];
        std::unique_ptr<ElementView> cur = iter.Parent();
        int cur_depth = depth + 1;
        while (cur) {
            if (TestMatcher(*cur, next, false, cur_depth, vsg) &&
                MatchParents(*cur, cur_depth, parents, mi + 2, vsg)) {
                return true;
            }
            cur = cur->Parent();
            ++cur_depth;
        }
        return false;
    }

    std::unique_ptr<ElementView> parent = iter.Parent();
    if (!parent) {
        return false;
    }
    if (!TestMatcher(*parent, m, false, depth + 1, vsg)) {
        return false;
    }
    return MatchParents(*parent, depth + 1, parents, mi + 1, vsg);
}

}  // namespace

bool MatchesChain(const ElementView& leaf,
                  const std::vector<ElementMatcher>& chain,
                  std::optional<VisualStateGroupRef>* vsg) {
    if (chain.empty() || chain.back().kind != ElementMatcher::Kind::Element) {
        return false;
    }
    std::optional<VisualStateGroupRef> found;
    if (!TestMatcher(leaf, chain.back(), true, 0, &found)) {
        return false;
    }
    std::vector<const ElementMatcher*> parents;
    parents.reserve(chain.size() - 1);
    for (size_t i = chain.size() - 1; i-- > 0;) {
        parents.push_back(&chain[i]);
    }
    if (!MatchParents(leaf, 0, parents, 0, &found)) {
        return false;
    }
    if (vsg) {
        *vsg = found;
    }
    return true;
}

std::vector<RuleMatch> FindMatchingRules(const ResolvedTheme& theme,
                                         const ElementView& leaf) {
    std::vector<RuleMatch> out;
    for (size_t i = theme.rules.size(); i-- > 0;) {
        const PreparedRule& rule = theme.rules[i];
        for (const auto& chain : rule.chains) {
            std::optional<VisualStateGroupRef> vsg;
            if (MatchesChain(leaf, chain, &vsg)) {
                out.push_back(RuleMatch{&rule, vsg});
                break;  // Chains are alternatives: one match is enough.
            }
        }
    }
    return out;
}

ResolvedTheme PrepareTheme(const Theme& theme) {
    ResolvedTheme out;
    out.id = theme.id;
    out.diagnostics = theme.diagnostics;

    ResolvedConstants constants = ResolveConstants(theme.constants);

    for (const auto& [name, value] : theme.resource_variables) {
        out.resource_variables[name] = ApplyStyleConstants(value, constants);
    }

    for (size_t i = 0; i < theme.rules.size(); ++i) {
        const ThemeRule& src = theme.rules[i];
        if (src.dead) {
            continue;  // Spec section 7.6: consumers MUST check `dead`.
        }
        PreparedRule rule;
        rule.source_index = i;
        rule.chains = src.selector;
        for (auto& chain : rule.chains) {
            for (auto& m : chain) {
                if (m.kind == ElementMatcher::Kind::Element && !m.type.empty()) {
                    m.type = AdjustTypeName(m.type);
                }
            }
        }
        for (const StyleRule& style : src.styles) {
            if (std::holds_alternative<CaptureRule>(style)) {
                ++out.skipped_captures;
                out.diagnostics.push_back(theme.id + L": " + src.target +
                                          L": capture rule skipped (Plano 3b)");
                continue;
            }
            const ValueRule& v = std::get<ValueRule>(style);
            if (v.IsDynamic()) {
                ++out.skipped_dynamic;
                out.diagnostics.push_back(theme.id + L": " + src.target + L": " +
                                          v.property_name +
                                          L": dynamic value skipped (Plano 3b)");
                continue;
            }
            PreparedStyle p;
            p.property = v.property_name;
            p.visual_state = v.visual_state;
            p.is_xaml = v.is_xaml_value;
            p.value = ApplyStyleConstants(v.value, constants);
            if (p.is_xaml) {
                bool rewritten = false;
                p.value = RewriteWindhawkBlur(p.value, &rewritten);
                if (rewritten) {
                    ++out.blur_approximations;
                }
            }
            rule.styles.push_back(std::move(p));
        }
        if (rule.styles.empty()) {
            continue;  // Nothing left to apply (e.g. only captures).
        }
        out.rules.push_back(std::move(rule));
    }
    return out;
}

}  // namespace styler
```

Nota sobre o teste `PrepareTheme … skips the unsupported`: ele espera `diagnostics.size() == 2` — uma linha por estilo pulado — e que a regra sobreviva com os dois estilos suportados. Se `ParseStyleRule(L"Height={{W}}")` do Plano 1 rejeitar `{{` (não deveria: é um `ValueRule` com `IsDynamic()`), ajuste o teste para construir o `ValueRule` à mão, não o parser.

`src/core/CMakeLists.txt` ganha `matcher.cpp`; `tests/core/CMakeLists.txt` ganha `test_matcher.cpp`.

- [ ] **Step 5: Rodar e ver passar**

Run: `cmake --build build && ctest --test-dir build -R core --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core
git commit -m "feat(core): matcher puro sobre ElementView e preparacao do tema"
```

---

### Task 4: TAP — registro de elementos, fila de liberação e a assinatura permanente

A assinatura que fica de pé. Ela é o que o Plano 2 deliberadamente não fez, e o motivo está no §7.2: o callback chega de dentro da varredura do XAML, então **nada é liberado no callback** — cada handle vai para uma fila `thread_local` que um `DispatcherQueueTimer` de disparo único drena quando a rajada de reports para. Esta task não aplica estilo nenhum: ela entrega o motor ligado em ponto morto, com um stub de `style_engine` que só loga, para a Task 5 preencher.

**Files:**
- Create: `src/tap/element_registry.h`, `src/tap/element_registry.cpp`, `src/tap/release_policy.h`, `src/tap/release_policy.cpp`, `src/tap/release_queue.h`, `src/tap/release_queue.cpp`, `src/tap/change_subscription.h`, `src/tap/change_subscription.cpp`, `src/tap/style_engine.h`, `src/tap/style_engine.cpp` (stub)
- Create: `tests/tap/test_release_policy.cpp`
- Modify: `src/tap/CMakeLists.txt`, `tests/tap/CMakeLists.txt`, `src/tap/tap_boundary.cpp`

**Interfaces:**
- Consumes: `AcquireSession()`, `ReleaseHandle`, `ReleasedHandleCount` (Plano 2); `IsInitializedForCurrentThread()` (Plano 2); `InspectableFromRaw` (Task 1).
- Produces:
  - `enum class styler::tap::ElementId : unsigned long long { None = 0 };`
  - `ElementId GetOrCreateElementId(InstanceHandle, wf::IInspectable const&)`, `ElementId FindElementId(InstanceHandle)`, `void ForgetElementId(InstanceHandle)`, `bool ForgetElementIdIfDead(InstanceHandle)`, `void ReapDeadElementIdsIfNeeded()` — todos `thread_local` por trás.
  - `std::vector<unsigned long long> HandlesToRelease(std::vector<unsigned long long> pending, const std::function<bool(unsigned long long)>& has_state)` — puro, no lib estático.
  - `void QueueRelease(InstanceHandle)`, `void FlushReleasesIfQuiet()`, `void FlushReleasesNow()`.
  - `HRESULT StartSubscription()`, `void StopSubscription()`.
  - Stub do motor (a Task 5 substitui o corpo, **não a assinatura**): `void OnElementAdded(ElementId, wux::FrameworkElement const&, const wchar_t* reported_type)`, `void OnElementRemoved(ElementId)`, `bool ElementHasState(ElementId)`.

- [ ] **Step 1: Teste da política de liberação**

`tests/tap/test_release_policy.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/release_policy.h>

using styler::tap::HandlesToRelease;

TEST_CASE("deduplicates and sorts the pending handles") {
    auto out = HandlesToRelease({30, 10, 20, 10, 30}, [](auto) { return false; });
    CHECK(out == std::vector<unsigned long long>{10, 20, 30});
}

TEST_CASE("keeps handles that still have state held") {
    auto out = HandlesToRelease({1, 2, 3}, [](auto h) { return h == 2; });
    CHECK(out == std::vector<unsigned long long>{1, 3});
}

TEST_CASE("drops the zero handle a root parent reports") {
    auto out = HandlesToRelease({0, 5, 0}, [](auto) { return false; });
    CHECK(out == std::vector<unsigned long long>{5});
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `cmake --build build && ctest --test-dir build -R tap --output-on-failure`
Expected: falha de compilação — `tap/release_policy.h` não existe.

- [ ] **Step 3: `release_policy.h/.cpp` (lib estático)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <vector>

namespace styler::tap {

// The pure half of the deferred-release queue: which of the queued handles
// get released on this drain. A handle is queued once per report naming it
// (a parent once per child), so duplicates are collapsed; zero is never a
// handle; and a handle whose element still carries state stays held - an
// element released while styled would be destroyed with its customization
// state stranded, since no removal is reported for a handle whose runtime
// object is gone (upstream vendor:18318-18345).
std::vector<unsigned long long> HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state);

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_policy.h>

#include <algorithm>

namespace styler::tap {

std::vector<unsigned long long> HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state) {
    std::sort(pending.begin(), pending.end());
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
    std::vector<unsigned long long> out;
    out.reserve(pending.size());
    for (unsigned long long h : pending) {
        if (h != 0 && !has_state(h)) {
            out.push_back(h);
        }
    }
    return out;
}

}  // namespace styler::tap
```

`src/tap/CMakeLists.txt`: `release_policy.cpp` entra em `taskbar_styler_tap_lib`; `tests/tap/CMakeLists.txt` ganha `test_release_policy.cpp`. Rode `ctest -R tap`: PASS.

- [ ] **Step 4: `element_registry.h/.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tap/winrt_common.h>

namespace styler::tap {

// Identity for an element across reports. A handle is an address: a
// destroyed element can be replaced by one reporting the same handle, so an
// entry is only trusted while its weak reference still resolves to the
// element being asked about (upstream vendor:11841-11900).
enum class ElementId : unsigned long long { None = 0 };

// All thread_local: an element belongs to the UI thread that reported it.
ElementId GetOrCreateElementId(InstanceHandle handle,
                               wf::IInspectable const& element);
ElementId FindElementId(InstanceHandle handle);
void ForgetElementId(InstanceHandle handle);
// Erases the entry only if its element is gone; returns whether it did.
bool ForgetElementIdIfDead(InstanceHandle handle);
// Sweeps entries whose element died without a removal being reported
// (their diagnostics reference was handed back). Amortized: runs only once
// the map has doubled since the last sweep. Calls OnElementRemoved for each.
void ReapDeadElementIdsIfNeeded();

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/element_registry.h>

#include <unordered_map>
#include <vector>

#include <tap/log.h>
#include <tap/style_engine.h>

namespace styler::tap {
namespace {

struct Entry {
    ElementId id = ElementId::None;
    winrt::weak_ref<wf::IInspectable> element;
};

thread_local std::unordered_map<InstanceHandle, Entry> t_ids;
thread_local unsigned long long t_last_id = 0;
thread_local size_t t_reap_threshold = 64;

}  // namespace

ElementId GetOrCreateElementId(InstanceHandle handle,
                               wf::IInspectable const& element) {
    if (!handle || !element) {
        return ElementId::None;
    }
    // spike-standing-crash E4: no reference into t_ids may be held across
    // make_weak specifically. It queries the object for IWeakReferenceSource
    // and creates a new weak reference, which can re-enter XAML; that
    // reentrant call can report another mutation on this thread, which can
    // insert into or erase from t_ids and rehash it, leaving a held Entry&
    // dangling (upstream has the same Entry&-across-make_weak shape,
    // vendor:11853 - a shared latent defect, not something specific to us).
    // Look up, call make_weak with nothing borrowed from the map, then write
    // the result back.
    //
    // This is narrower than "no reference across any WinRT call": resolving
    // an EXISTING weak_ref via .get() (below, and in ForgetElementIdIfDead
    // and ReapDeadElementIdsIfNeeded) is a different, lighter operation - an
    // interlocked refcount-promotion attempt with no call into the object or
    // XAML - so it cannot report a mutation and cannot mutate the map.
    // Holding an iterator or a const reference across it is fine, and those
    // three sites do.
    {
        auto it = t_ids.find(handle);
        if (it != t_ids.end() && it->second.id != ElementId::None &&
            it->second.element.get() == element) {
            return it->second.id;
        }
    }
    ElementId id = static_cast<ElementId>(++t_last_id);
    winrt::weak_ref<wf::IInspectable> weak;
    try {
        weak = winrt::make_weak(element);
    } catch (winrt::hresult_error const& ex) {
        // Without a weak reference the entry cannot be told apart from a
        // successor at the same address; keep neither it nor the id.
        STYLER_LOG(LogLevel::Error, L"make_weak failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
        t_ids.erase(handle);
        return ElementId::None;
    }
    Entry& entry = t_ids[handle];
    entry.id = id;
    entry.element = std::move(weak);
    return id;
}

ElementId FindElementId(InstanceHandle handle) {
    auto it = t_ids.find(handle);
    return it != t_ids.end() ? it->second.id : ElementId::None;
}

void ForgetElementId(InstanceHandle handle) {
    t_ids.erase(handle);
}

bool ForgetElementIdIfDead(InstanceHandle handle) {
    auto it = t_ids.find(handle);
    if (it == t_ids.end()) {
        return false;
    }
    if (it->second.element.get()) {
        return false;  // Still alive: a later report may name it again.
    }
    t_ids.erase(it);
    return true;
}

void ReapDeadElementIdsIfNeeded() {
    if (t_ids.size() < t_reap_threshold) {
        return;
    }
    std::vector<std::pair<InstanceHandle, ElementId>> dead;
    for (const auto& [handle, entry] : t_ids) {
        if (!entry.element.get()) {
            dead.push_back({handle, entry.id});
        }
    }
    for (const auto& [handle, id] : dead) {
        t_ids.erase(handle);
        OnElementRemoved(id);  // Tear down as its removal would have.
    }
    t_reap_threshold = t_ids.size() * 2 + 64;
    if (!dead.empty()) {
        STYLER_LOG(LogLevel::Debug, L"reaped %zu dead element ids", dead.size());
    }
}

}  // namespace styler::tap
```

- [ ] **Step 5: `release_queue.h/.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <xamlom.h>

namespace styler::tap {

// The deferred-release queue, one per UI thread. Reports arrive from inside
// XAML's own Enter/Leave walks; releasing a handle there re-enters the
// diagnostics while the tree is being mutated and destroys an element the
// walk is still visiting. So a report only queues, and the drain runs on the
// thread's dispatcher once the burst of reports has been quiet for
// kQuietMs. Mirrors upstream vendor:18281-18430.
void QueueRelease(InstanceHandle handle);

// Arms the one-shot drain timer when the queue is non-empty, no drain is
// already armed, and the last queue happened at least kQuietMs ago. Cheap;
// called at the end of every report.
void FlushReleasesIfQuiet();

// Drains synchronously. Only legal OUTSIDE any XAML callback, on the queue's
// own thread (e.g. from the timer tick). Never call it from
// OnVisualTreeChange.
void FlushReleasesNow();

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_queue.h>

#include <chrono>
#include <vector>

#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/release_policy.h>
#include <tap/style_engine.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

constexpr ULONGLONG kQuietMs = 200;  // Long enough to sit out a tree build.
constexpr unsigned kDrainDelayMs = 1;  // Only has to leave the report's frame.

thread_local std::vector<unsigned long long> t_pending;
thread_local ULONGLONG t_last_queue_tick = 0;
thread_local bool t_drain_armed = false;
thread_local bool t_no_dispatcher_logged = false;
thread_local winrt::Windows::System::DispatcherQueueTimer t_timer{nullptr};
thread_local winrt::event_token t_tick_token{};

}  // namespace

void QueueRelease(InstanceHandle handle) {
    if (!handle) {
        return;
    }
    t_pending.push_back(handle);
    t_last_queue_tick = GetTickCount64();
}

void FlushReleasesNow() {
    t_drain_armed = false;
    std::vector<unsigned long long> pending = std::move(t_pending);
    t_pending.clear();
    const size_t pending_count = pending.size();

    std::vector<unsigned long long> to_release =
        HandlesToRelease(std::move(pending), [](unsigned long long h) {
            return ElementHasState(FindElementId(h));
        });
    for (unsigned long long h : to_release) {
        ReleaseHandle(h);
        ForgetElementIdIfDead(h);
    }
    // Releases above are what let elements die unreported; sweep for those
    // here, outside any walk.
    ReapDeadElementIdsIfNeeded();
    // "held" is the live-handle gauge spec section 7.2 asks for: handles we
    // deliberately keep because their element carries state. It must not
    // grow while the taskbar sits still - docs/smoke-test.md reads it.
    STYLER_LOG(LogLevel::Info, L"drained %zu handles, %zu held (%ld released so far)",
               to_release.size(), pending_count - to_release.size(),
               ReleasedHandleCount());
}

void FlushReleasesIfQuiet() {
    if (t_pending.empty() || t_drain_armed ||
        GetTickCount64() - t_last_queue_tick < kQuietMs) {
        return;
    }
    try {
        if (!t_timer) {
            auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            if (!queue) {
                // Releasing from here is the one thing that isn't safe, so
                // the elements stay held. Said once per thread.
                if (!t_no_dispatcher_logged) {
                    t_no_dispatcher_logged = true;
                    STYLER_LOG(LogLevel::Error,
                               L"no DispatcherQueue on thread %lu: %zu handles held",
                               GetCurrentThreadId(), t_pending.size());
                }
                return;
            }
            t_timer = queue.CreateTimer();
            t_timer.IsRepeating(false);
            t_timer.Interval(std::chrono::milliseconds{kDrainDelayMs});
            t_tick_token = t_timer.Tick(
                [](winrt::Windows::System::DispatcherQueueTimer const&,
                   wf::IInspectable const&) {
                    try {
                        FlushReleasesNow();
                    } catch (winrt::hresult_error const& ex) {
                        STYLER_LOG(LogLevel::Error, L"drain hresult 0x%08X",
                                   static_cast<unsigned>(ex.code()));
                    } catch (...) {
                        STYLER_LOG(LogLevel::Error, L"drain threw");
                    }
                });
        }
        t_timer.Start();
        t_drain_armed = true;
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"arming drain failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"arming drain threw");
    }
}

}  // namespace styler::tap
```

Sobre o timer `thread_local`: é um objeto WinRT com destrutor não trivial em armazenamento de thread. Ao contrário do escopo de namespace (Ruling 11 do Plano 2), um `thread_local` só é destruído quando **a thread** termina — e as threads de UI do explorer só terminam com o processo, quando o teardown é o menor dos problemas. O upstream usa exatamente este arranjo (`vendor:18284`). Não troque por um ponteiro vazado sem ler isto.

- [ ] **Step 6: O stub do motor, `style_engine.h/.cpp`**

O header é o contrato que a Task 5 preenche; o `.cpp` desta task só loga.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// Called from the standing subscription, on the reporting thread, for every
// element reported as added (once it has an id and is a FrameworkElement)
// and removed. `reported_type` is the diagnostics' Type string - the matcher
// accepts it as an alternative to the runtime class name for the leaf.
void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type);
void OnElementRemoved(ElementId id);

// Whether this thread's engine holds customization state for `id`. The
// release drain keeps such handles held (release_policy.h).
bool ElementHasState(ElementId id);

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_engine.h>

#include <tap/log.h>

namespace styler::tap {

// Task 4 stub: the subscription is wired and drained, nothing is styled yet.
// Task 5 replaces this file's bodies, not its signatures.

void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type) {
    STYLER_LOG(LogLevel::Debug, L"add %llu %s (%s)",
               static_cast<unsigned long long>(id),
               winrt::get_class_name(element).c_str(),
               reported_type ? reported_type : L"");
}

void OnElementRemoved(ElementId id) {
    STYLER_LOG(LogLevel::Debug, L"remove %llu",
               static_cast<unsigned long long>(id));
}

bool ElementHasState(ElementId) {
    return false;
}

}  // namespace styler::tap
```

- [ ] **Step 7: `change_subscription.h/.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

namespace styler::tap {

// Subscribes to IVisualTreeService3::AdviseVisualTreeChange for the life of
// the session.
//
// Two upstream findings shape this (both from
// vendor/upstream/windows-11-taskbar-styler.wh.cpp, and both reproduced here
// the hard way - see spike-standing-crash.md, the post-mortem for why the
// first version of this task, `b919419`, crashed explorer.exe):
//
// - Composition diagnostics corrupt the heap (vendor:10904-10914). XAML
//   creates a second, unrelated diagnostics object for every
//   Windows.UI.Composition.* visual it reports (a DirectComposition visual,
//   not a XAML element), and that object rebuilds a process-wide walker with
//   no locking whenever one is added on ANY explorer UI thread - Task View,
//   for example - while another thread is inside the same code. Upstream
//   keeps it from ever being created by answering XAML's one registry read
//   (made once, from inside AdviseVisualTreeChange) through inline hooks on
//   RegOpenKeyExW/RegQueryValueExW; this project allows no injection APIs,
//   so StartSubscription instead requires the real
//   HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag value already
//   be 1 - written once, with the user's consent, by `taskbar-styler setup`
//   (elevated) - and fails closed (E_NOT_VALID_STATE, no subscription;
//   exporting the tree still works) when it is not. change_subscription.cpp
//   also never resolves a reported Windows.UI.Composition.* handle even
//   when the value is set, as defense in depth: that filter alone stops the
//   deterministic crash (resolving one such handle is what kills the
//   process), but only the registry value stops the probabilistic heap
//   race, since that race happens inside
//   XamlDiagnostics::CreateCompVisualDiag, before our callback ever runs.
// - Calling AdviseVisualTreeChange from the calling (UI) thread hangs in
//   Advising::RunOnUIThread "sometimes" (vendor:11013-11030) - measured here
//   too (process stayed alive but stopped responding, not a crash). Upstream
//   calls Advise from a new thread instead; StartSubscription does the same
//   via CreateThread and returns once that thread exists, without waiting
//   for the initial flood - it still arrives synchronously inside Advise,
//   just on that new thread rather than the caller's.
//
// Idempotent: a second Start with a live subscription is a no-op that
// returns S_FALSE.
HRESULT StartSubscription();

// Unadvises. If Unadvise fails the callback object is leaked on purpose -
// XAML may still call into it, and a freed vtable inside explorer is worse
// than one small leak (same stance as ReleaseOnExit in tree_export.cpp).
void StopSubscription();

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/change_subscription.h>

#include <atomic>
#include <cwchar>
#include <memory>

#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/release_queue.h>
#include <tap/style_engine.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

// A Windows.UI.Composition.* handle is never resolved - see
// change_subscription.h for why. Its handles are still queued for release
// exactly like any other Add.
constexpr wchar_t kCompositionPrefix[] = L"Windows.UI.Composition.";
constexpr size_t kCompositionPrefixLen =
    (sizeof(kCompositionPrefix) / sizeof(wchar_t)) - 1;

// The standing callback. Heap-allocated with a real reference count: XAML
// holds one reference while advised, we hold one while subscribed. Unlike
// tree_export.cpp's stack snapshot this object lives for hours.
class StandingCallback : public IVisualTreeServiceCallback2 {
public:
    explicit StandingCallback(std::shared_ptr<DiagnosticsSession> session)
        : session_(std::move(session)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown ||
            riid == __uuidof(IVisualTreeServiceCallback) ||
            riid == __uuidof(IVisualTreeServiceCallback2)) {
            *ppv = static_cast<IVisualTreeServiceCallback2*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++ref_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = static_cast<ULONG>(--ref_);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    // Everything here runs on the reporting UI thread, inside XAML's walk.
    // Order matters and mirrors upstream vendor:11100-11185: style work
    // first (its own try, so the bookkeeping below still runs if it
    // throws), then arm the drain, then queue this report's handles.
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation, VisualElement element,
        VisualMutationType mutationType) override {
        try {
            if (!IsInitializedForCurrentThread()) {
                // A XAML thread SetSite never initialized (not a taskbar
                // host). Upstream returns here too, before queueing: those
                // handles stay registered. Bounded by explorer's own life.
                STYLER_LOG(LogLevel::Debug, L"report on uninitialized thread %lu",
                           GetCurrentThreadId());
                return S_OK;
            }
            try {
                if (mutationType == Add) {
                    if (element.Type &&
                        wcsncmp(element.Type, kCompositionPrefix,
                                kCompositionPrefixLen) == 0) {
                        // vendor:10904-10914: resolving this handle is what
                        // crashes the process. Never call
                        // GetIInspectableFromHandle on it.
                        STYLER_LOG(LogLevel::Debug,
                                   L"skipped composition visual %s", element.Type);
                    } else {
                        ::IInspectable* raw = nullptr;
                        HRESULT hr = session_->diagnostics()->GetIInspectableFromHandle(
                            element.Handle, &raw);
                        if (SUCCEEDED(hr) && raw) {
                            wf::IInspectable obj = InspectableFromRaw(raw);
                            ElementId id = GetOrCreateElementId(element.Handle, obj);
                            if (id != ElementId::None) {
                                if (auto fe = obj.try_as<wux::FrameworkElement>()) {
                                    OnElementAdded(id, fe, element.Type);
                                }
                            }
                        }
                    }
                } else if (mutationType == Remove) {
                    OnElementRemoved(FindElementId(element.Handle));
                }
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"report hresult 0x%08X",
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
                STYLER_LOG(LogLevel::Error, L"report threw");
            }

            FlushReleasesIfQuiet();

            if (mutationType == Add) {
                QueueRelease(element.Handle);
                QueueRelease(relation.Parent);
            } else if (mutationType == Remove) {
                // Queued, never released here: this report arrives from
                // inside the Leave walk still visiting the removed subtree.
                QueueRelease(element.Handle);
                ForgetElementId(element.Handle);
            }
        } catch (...) {
            // Nothing above should reach here; if it does, XAML must not
            // see an error - it would stop reporting.
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }

private:
    std::atomic<long> ref_{1};
    std::shared_ptr<DiagnosticsSession> session_;
};

struct Subscription {
    StandingCallback* callback = nullptr;  // Our reference.
    IVisualTreeService3* service = nullptr;
};

// Heap-leaked for the same reason as g_session: no namespace-scope
// destructor may run at DLL_PROCESS_DETACH (Plano 2, Ruling 11).
auto* const g_subscription = new std::atomic<Subscription*>{nullptr};

}  // namespace

HRESULT StartSubscription() {
    if (g_subscription->load()) {
        return S_FALSE;
    }

    // Fail closed: see the header comment for why. The Debug filter above is
    // defense in depth, not a substitute for this - it only stops the
    // deterministic crash, not the heap race, which happens before our
    // callback is ever called.
    DWORD disabled = 0;
    DWORD disabled_size = sizeof(disabled);
    LONG reg_st = RegGetValueW(
        HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\XAML\\Debug",
        L"DisableCompositionDiag", RRF_RT_REG_DWORD, nullptr, &disabled,
        &disabled_size);
    if (reg_st != ERROR_SUCCESS || disabled != 1) {
        STYLER_LOG(LogLevel::Error,
                   L"composition diagnostics are enabled - run \"taskbar-styler "
                   L"setup\" once (as administrator) to disable them; not "
                   L"subscribing");
        return E_NOT_VALID_STATE;
    }

    std::shared_ptr<DiagnosticsSession> session = AcquireSession();
    if (!session) {
        return E_NOT_VALID_STATE;
    }
    IVisualTreeService3* service = nullptr;
    HRESULT hr = session->diagnostics()->QueryInterface(
        __uuidof(IVisualTreeService3), reinterpret_cast<void**>(&service));
    if (FAILED(hr) || !service) {
        STYLER_LOG(LogLevel::Error, L"QI IVisualTreeService3 failed 0x%08X",
                   static_cast<unsigned>(hr));
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }
    auto* sub = new (std::nothrow) Subscription{};
    if (!sub) {
        service->Release();
        return E_OUTOFMEMORY;
    }
    sub->callback = new (std::nothrow) StandingCallback(session);
    if (!sub->callback) {
        service->Release();
        delete sub;
        return E_OUTOFMEMORY;
    }
    sub->service = service;

    Subscription* expected = nullptr;
    if (!g_subscription->compare_exchange_strong(expected, sub)) {
        sub->callback->Release();
        service->Release();
        delete sub;
        return S_FALSE;  // Lost the race to another SetSite.
    }

    // vendor:11013-11030: calling Advise from this thread hangs in
    // Advising::RunOnUIThread "sometimes" - measured here too. Run it on a
    // new thread instead, and do not wait for it: the initial flood still
    // arrives synchronously inside Advise, just on that thread.
    sub->callback->AddRef();  // The thread's reference; released when it ends.
    HANDLE thread = CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto* s = static_cast<Subscription*>(param);
            HRESULT advise_hr = s->service->AdviseVisualTreeChange(s->callback);
            if (FAILED(advise_hr)) {
                STYLER_LOG(LogLevel::Error,
                           L"AdviseVisualTreeChange failed 0x%08X",
                           static_cast<unsigned>(advise_hr));
                g_subscription->store(nullptr);
                // Advise can register, walk, and then fail: unadvise
                // regardless and keep the callback alive if that fails too
                // (same stance as ReleaseOnExit in tree_export.cpp).
                if (SUCCEEDED(
                        s->service->UnadviseVisualTreeChange(s->callback))) {
                    s->callback->Release();  // Ours.
                }
                s->service->Release();
                s->callback->Release();  // The thread's reference.
                delete s;
                return 0;
            }
            STYLER_LOG(LogLevel::Info, L"subscription started on thread %lu",
                       GetCurrentThreadId());
            s->callback->Release();  // The thread's reference only - ours
                                      // stays live until StopSubscription.
            return 0;
        },
        sub, 0, nullptr);
    if (!thread) {
        DWORD err = GetLastError();
        STYLER_LOG(LogLevel::Error, L"CreateThread for Advise failed %lu", err);
        g_subscription->store(nullptr);
        sub->callback->Release();  // The thread's reference, never started.
        sub->callback->Release();  // Ours.
        service->Release();
        delete sub;
        return HRESULT_FROM_WIN32(err);
    }
    CloseHandle(thread);
    STYLER_LOG(LogLevel::Info, L"advise thread created");
    return S_OK;
}

void StopSubscription() {
    Subscription* sub = g_subscription->exchange(nullptr);
    if (!sub) {
        return;
    }
    HRESULT hr = sub->service->UnadviseVisualTreeChange(sub->callback);
    if (SUCCEEDED(hr)) {
        sub->callback->Release();
        STYLER_LOG(LogLevel::Info, L"subscription stopped");
    } else {
        STYLER_LOG(LogLevel::Error,
                   L"UnadviseVisualTreeChange failed 0x%08X - leaking the callback",
                   static_cast<unsigned>(hr));
    }
    sub->service->Release();
    delete sub;
}

}  // namespace styler::tap
```

- [ ] **Step 8: Ligar no `SetSite`, o registro de elementos sem referência através de `make_weak`, e o CLI `setup`**

Em `tap_boundary.cpp`, incluir `<tap/change_subscription.h>`. No `SetSite`, **depois** do bloco do `ExportTreeToFile` e do `ProbeWinRt` (o instantâneo faz o próprio Advise/Unadvise e precisa ter terminado antes de a assinatura permanente começar — duas assinaturas ao mesmo tempo é território não medido):

```cpp
                HRESULT sub_hr = StartSubscription();
                if (FAILED(sub_hr)) {
                    STYLER_LOG(LogLevel::Error, L"StartSubscription failed 0x%08X",
                               static_cast<unsigned>(sub_hr));
                }
```

Com um site não-nulo, **antes** de `OpenDiagnostics(site)`: `StopSubscription();` — um segundo `SetSite(site)` (por exemplo um segundo `taskbar-styler load` sem reiniciar o Explorer) não pode deixar `OpenDiagnostics` fechar e reabrir a sessão de diagnóstico com a assinatura antiga ainda registrada contra a antiga: sem isso, `g_subscription` fica preso a um `service`/`callback` mortos e nenhuma assinatura nova consegue subir até o Explorer reiniciar (medido no spike, "segundo load").

No ramo `if (!site)`, antes de `CloseDiagnostics()`: `StopSubscription();` (inalterado).

`element_registry.cpp`: `GetOrCreateElementId` não pode segurar uma referência para dentro de `t_ids` através de `winrt::make_weak` — a chamada reentra no XAML, que pode reportar outra mutação nesta mesma thread, e esse reporte pode inserir ou apagar em `t_ids` e re-hashear o mapa, deixando a referência pendurada. Procure com `find`, monte o `weak_ref` numa local, e só então escreva de volta.

CLI (`src/cli/main.cpp`): comando `setup` — grava `HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag=1` via `reg.exe` elevado (`ShellExecuteExW`, verbo `runas`), explica o que o valor faz e como desfazer. `status` mostra se está em 1; `load` avisa quando não está ("o TAP vai exportar a árvore, mas não vai assinar mudanças").

`src/tap/CMakeLists.txt`: os novos `.cpp` (menos `release_policy.cpp`) entram no alvo MODULE; `Advapi32.lib` entra no link do MODULE (`RegGetValueW`). `src/cli/CMakeLists.txt`: `Advapi32.lib` e `Shell32.lib` entram no link do CLI (`RegGetValueW`, `ShellExecuteExW`).

- [ ] **Step 9: Build, testes e smoke**

Antes de tudo: `taskbar-styler.exe setup`, uma vez, elevado — sem `DisableCompositionDiag=1` a assinatura permanente se recusa a começar (de propósito).

Reinicie o Explorer, então `cmake --build build`, `ctest --test-dir build --output-on-failure`, `build\src\cli\taskbar-styler.exe load`.

No log, na ordem: `tree exported …`, `winrt ok …`, `advise thread created`, `subscription started on thread <N>` — **N é uma thread diferente** da que rodou `SetSite` (a Advise agora roda numa thread criada com `CreateThread`, nunca na pilha do `SetSite`). Depois, com `Debug` ligado (edite `SetLogLevel` temporariamente ou espere a Task 7 expor o nível no config — para esta task, um `SetLogLevel(LogLevel::Debug)` provisório no `SetSite` é aceitável **desde que saia antes do commit**): centenas de `add …`, **zero** `skipped composition visual` (a chave do registro já impede as diagnostics de composition de existirem — o filtro só pula algo se a chave não estiver em 1), e ~200 ms depois um `drained N handles, M held (K released so far)`. Abra o menu Iniciar, a central de notificações e o Task View: novos `add`, e outro `drained`, sem nenhuma linha `ERR` e sem o Explorer travar (`Responding: True`) ou cair. Rode `load` de novo, sem reiniciar o Explorer: espere `subscription stopped` seguido de um novo `subscription started` (a Task View pode deixar uma rajada sem drenar na sua própria thread, se nenhuma mutação nova chegar nela depois para reavaliar a fila — comportamento pré-existente do dreno adiado, não uma regressão desta correção). Anote no relatório os três números (elementos do lote inicial, liberados após o primeiro dreno, liberados após abrir o Iniciar).

O que **não** dá para exercitar sem tocar em HKLM de propósito: o ramo de falha do portão do registro (teste local, com o nome do valor trocado para algo inexistente, revertido antes do commit).

- [ ] **Step 10: Commit**

```bash
git add src/tap tests/tap
git commit -m "feat(tap): assinatura permanente com registro de elementos e dreno adiado de handles"
```

---

### Task 5: TAP — o motor de estilo: aplicar e restaurar

A entrega do plano em sua forma mínima: com `%APPDATA%\TaskbarStyler\config.json` apontando para um tema, `load` estiliza a taskbar; a Task 7 traz o CLI e a recarga. Estados visuais (`@VisualState`, `@VSG`) ficam para a Task 6 — aqui esses estilos são **pulados e contados**, nunca aplicados sem condição.

**Files:**
- Create: `src/tap/property_setter.h`, `src/tap/property_setter.cpp`, `src/tap/theme_session.h`, `src/tap/theme_session.cpp`, `src/core/include/styler/config.h`, `src/core/config.cpp`
- Create: `tests/core/test_config.cpp`
- Modify: `src/tap/style_engine.h`, `src/tap/style_engine.cpp` (substitui o stub), `src/tap/tap_boundary.cpp`, `src/tap/CMakeLists.txt`, `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `styler::ResolvedTheme`, `PrepareTheme`, `ElementView`, `FindMatchingRules`, `PreparedStyle` (Task 3); `ElementId`, registro (Task 4); `InitializationData()` (Task 1); `LoadThemeFromFile` (Plano 1).
- Produces:
  - `struct styler::Config { std::wstring theme; std::wstring log_level; }`, `Config styler::ParseConfigJson(std::string_view utf8)` (lança `ParseError`), `std::string styler::SerializeConfigJson(const Config&)`.
  - `void styler::tap::SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme)` / `std::shared_ptr<const styler::ResolvedTheme> CurrentTheme()` — global, atômico, imutável.
  - `void styler::tap::RestoreAllOnThisThread()` — desfaz tudo que esta thread aplicou.
  - `HRESULT styler::tap::LoadConfiguredTheme()` — lê config + JSON do tema, prepara, `SetTheme`; `S_FALSE` sem tema configurado.
  - `std::wstring styler::tap::ConfigPath()` — `%APPDATA%\TaskbarStyler\config.json`.
  - `property_setter.h`: `ResolvedSetter`, `ResolveSetter`, `ReadLocalValueWithWorkaround`, `SetOrClearValue`, `EscapeXmlAttribute`.

- [ ] **Step 1: Config no core — teste e implementação**

`tests/core/test_config.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/config.h>
#include <styler/selector.h>  // ParseError

using styler::Config;
using styler::ParseConfigJson;
using styler::SerializeConfigJson;

TEST_CASE("parses theme and log level") {
    Config c = ParseConfigJson(R"({"theme":"TranslucentTaskbar","logLevel":"debug"})");
    CHECK(c.theme == L"TranslucentTaskbar");
    CHECK(c.log_level == L"debug");
}

TEST_CASE("missing fields default to empty") {
    Config c = ParseConfigJson("{}");
    CHECK(c.theme.empty());
    CHECK(c.log_level.empty());
}

TEST_CASE("malformed json is a ParseError") {
    CHECK_THROWS_AS(ParseConfigJson("{nope"), styler::ParseError);
    CHECK_THROWS_AS(ParseConfigJson(R"({"theme": 5})"), styler::ParseError);
}

TEST_CASE("serialize round-trips") {
    Config c;
    c.theme = L"Aeris";
    c.log_level = L"info";
    Config back = ParseConfigJson(SerializeConfigJson(c));
    CHECK(back.theme == L"Aeris");
    CHECK(back.log_level == L"info");
}
```

`src/core/include/styler/config.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// The one file that crosses the process boundary, Tray/CLI -> TAP (spec
// section 4.2). `theme` is a theme id (file stem under themes/); empty means
// "no theme". `log_level` is "error", "info" or "debug"; empty keeps the
// TAP's default.
struct Config {
    std::wstring theme;
    std::wstring log_level;
};

Config ParseConfigJson(std::string_view utf8);  // Throws ParseError.
std::string SerializeConfigJson(const Config& config);

}  // namespace styler
```

`src/core/config.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/config.h>

#include <nlohmann/json.hpp>
#include <styler/selector.h>
#include <styler/utf.h>

namespace styler {

Config ParseConfigJson(std::string_view utf8) {
    nlohmann::json j = nlohmann::json::parse(utf8, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        throw ParseError("config: not a JSON object");
    }
    Config c;
    if (auto it = j.find("theme"); it != j.end()) {
        if (!it->is_string()) {
            throw ParseError("config: \"theme\" must be a string");
        }
        c.theme = Utf8ToWide(it->get<std::string>());
    }
    if (auto it = j.find("logLevel"); it != j.end()) {
        if (!it->is_string()) {
            throw ParseError("config: \"logLevel\" must be a string");
        }
        c.log_level = Utf8ToWide(it->get<std::string>());
    }
    return c;
}

std::string SerializeConfigJson(const Config& config) {
    nlohmann::json j;
    j["theme"] = WideToUtf8(config.theme);
    j["logLevel"] = WideToUtf8(config.log_level);
    return j.dump(2) + "\n";
}

}  // namespace styler
```

`src/core/CMakeLists.txt` ganha `config.cpp`; `tests/core/CMakeLists.txt` ganha `test_config.cpp`. `ctest -R core`: PASS.

- [ ] **Step 2: `property_setter.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include <styler/matcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {

std::wstring EscapeXmlAttribute(std::wstring_view text);

struct ResolvedSetter {
    wux::DependencyProperty property{nullptr};
    wf::IInspectable value;  // Null when `clear` is set.
    bool clear = false;      // `Prop:=` with an empty value clears the property.
};

// Turns a property name into a DependencyProperty - and the style's text
// into a value XAML parsed - by loading a <Style TargetType="type"> with one
// <Setter> through XamlReader and reading it back (upstream
// vendor:15394-15440, :15595-15632). This is also how attached properties
// (Canvas.ZIndex, Grid.Column) and `:=` markup values resolve. On XAML's
// 0x802B000A ("cannot create a System.Type from the text") the load is
// retried with `fallback_type`, then with "FrameworkElement". Throws
// winrt::hresult_error when every attempt fails.
ResolvedSetter ResolveSetter(std::wstring_view type,
                             std::wstring_view fallback_type,
                             const styler::PreparedStyle& style);

// ReadLocalValue, except that a BindingExpression(Base) - observed for
// properties declared as {TemplateBinding ...} - is replaced by
// GetAnimationBaseValue, since SetValue with a binding expression fails and
// the original could never be restored (upstream vendor:12377-12400).
wf::IInspectable ReadLocalValueWithWorkaround(
    wux::DependencyObject const& object, wux::DependencyProperty const& property);

// SetValue, or ClearValue when `value` is DependencyProperty::UnsetValue().
// `initial_apply` enables the one deferral upstream needs: setting
// Rectangle#BackgroundFill.Fill before TaskbarBackground's OnApplyTemplate
// can crash, so that first set is posted to the element's dispatcher at High
// priority instead (vendor:14940-14990).
void SetOrClearValue(wux::DependencyObject const& object,
                     wux::DependencyProperty const& property,
                     wf::IInspectable const& value, bool initial_apply);

// Whether the current thread is inside a write this engine made itself, so
// that write's own PropertyChanged callback does not react to it as if the
// shell had changed the value. Every direct SetValue/ClearValue on a
// property this engine tracks - including SetOrClearValue's own deferred
// BackgroundFill.Fill set, which runs later, on the dispatcher, outside
// whatever scope its caller held - must be wrapped in a ModifyingGuard
// while it runs (review round 1, C2: the deferred set used to run
// unguarded, so its own PropertyChanged notification looked external and
// overwrote the tracked `original` with the engine's own value - restore
// then "restored" to that, never to the shell's).
bool IsModifying();

// RAII for the flag IsModifying() reads: true for the guard's lifetime,
// false again on scope exit - including via an exception, which a
// hand-paired `t_modifying = true; ...; t_modifying = false;` cannot
// guarantee (review round 1, C1: the original ApplyProperty call had no
// try/catch at all, so a throw left the flag stuck true for the rest of
// the thread's life).
class ModifyingGuard {
public:
    ModifyingGuard();
    ~ModifyingGuard();
    ModifyingGuard(const ModifyingGuard&) = delete;
    ModifyingGuard& operator=(const ModifyingGuard&) = delete;
};

}  // namespace styler::tap
```

- [ ] **Step 3: `property_setter.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/property_setter.h>

#include <utility>
#include <vector>

#include <tap/log.h>

namespace styler::tap {
namespace {

// XAML's "Failed to create a 'System.Type' from the text ..." (a stowed
// exception): the TargetType is unknown to the parser.
constexpr HRESULT kUnknownTypeInXaml = 0x802B000A;

wux::Style LoadStyle(std::wstring_view type, const std::wstring& setter_xaml) {
    std::wstring xaml =
        LR"(<ResourceDictionary
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:muxc="using:Microsoft.UI.Xaml.Controls")";
    if (auto dot = type.rfind(L'.'); dot != std::wstring_view::npos) {
        xaml += L"\n    xmlns:styler=\"using:";
        xaml += EscapeXmlAttribute(type.substr(0, dot));
        xaml += L"\">\n    <Style TargetType=\"styler:";
        xaml += EscapeXmlAttribute(type.substr(dot + 1));
        xaml += L"\">\n";
    } else {
        xaml += L">\n    <Style TargetType=\"";
        xaml += EscapeXmlAttribute(type);
        xaml += L"\">\n";
    }
    xaml += setter_xaml;
    xaml += L"    </Style>\n</ResourceDictionary>";

    auto dictionary = wux::Markup::XamlReader::Load(xaml).as<wux::ResourceDictionary>();
    auto first = dictionary.First();
    return first.Current().Value().as<wux::Style>();
}

wux::Style LoadStyleWithFallbacks(std::wstring_view type,
                                  std::wstring_view fallback_type,
                                  const std::wstring& setter_xaml) {
    std::vector<std::wstring_view> attempts;
    if (!type.empty()) {
        attempts.push_back(type);
    }
    if (!fallback_type.empty() && fallback_type != type) {
        attempts.push_back(fallback_type);
    }
    attempts.push_back(L"FrameworkElement");
    for (size_t i = 0; i < attempts.size(); ++i) {
        try {
            return LoadStyle(attempts[i], setter_xaml);
        } catch (winrt::hresult_error const& ex) {
            if (ex.code() != kUnknownTypeInXaml || i + 1 == attempts.size()) {
                throw;
            }
            STYLER_LOG(LogLevel::Debug, L"type %.*s unknown to XAML, retrying",
                       static_cast<int>(attempts[i].size()), attempts[i].data());
        }
    }
    throw winrt::hresult_error(E_UNEXPECTED);
}

// Set while this thread is inside a write ApplyProperty/RestoreElement (or
// this file's own deferred BackgroundFill.Fill callback) made itself. See
// IsModifying()/ModifyingGuard in property_setter.h.
thread_local bool t_modifying = false;

// Pending deferred BackgroundFill sets on this thread, so a second apply to
// the same element cancels the first instead of racing it.
// CoreDispatcher::TryRunAsync returns IAsyncOperation<bool> (whether the
// callback got to run), not IAsyncAction - the type below was measured
// against the real SDK header, not assumed.
thread_local std::vector<std::pair<winrt::weak_ref<wux::DependencyObject>,
                                   wf::IAsyncOperation<bool>>>
    t_delayed_fill;

bool IsBackgroundFill(wux::DependencyObject const& object,
                      wux::DependencyProperty const& property) {
    if (property != wux::Shapes::Shape::FillProperty()) {
        return false;
    }
    auto fe = object.try_as<wux::FrameworkElement>();
    return fe && fe.Name() == L"BackgroundFill" &&
           winrt::get_class_name(object) == L"Windows.UI.Xaml.Shapes.Rectangle";
}

}  // namespace

std::wstring EscapeXmlAttribute(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'"': out += L"&quot;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

ResolvedSetter ResolveSetter(std::wstring_view type,
                             std::wstring_view fallback_type,
                             const styler::PreparedStyle& style) {
    std::wstring xaml = L"        <Setter Property=\"";
    xaml += EscapeXmlAttribute(style.property);
    xaml += L"\"";
    const bool clear = style.is_xaml && style.value.find_first_not_of(L" \t") ==
                                            std::wstring::npos;
    if (clear) {
        xaml += L" Value=\"{x:Null}\" />\n";
    } else if (!style.is_xaml) {
        xaml += L" Value=\"";
        xaml += EscapeXmlAttribute(style.value);
        xaml += L"\" />\n";
    } else {
        xaml += L">\n            <Setter.Value>\n";
        xaml += style.value;
        xaml += L"\n            </Setter.Value>\n        </Setter>\n";
    }

    wux::Style s = LoadStyleWithFallbacks(type, fallback_type, xaml);
    auto setter = s.Setters().GetAt(0).as<wux::Setter>();
    ResolvedSetter out;
    out.property = setter.Property();
    out.clear = clear;
    if (!clear) {
        out.value = setter.Value();
    }
    return out;
}

wf::IInspectable ReadLocalValueWithWorkaround(
    wux::DependencyObject const& object, wux::DependencyProperty const& property) {
    wf::IInspectable value = object.ReadLocalValue(property);
    if (value) {
        auto cls = winrt::get_class_name(value);
        if (cls == L"Windows.UI.Xaml.Data.BindingExpressionBase" ||
            cls == L"Windows.UI.Xaml.Data.BindingExpression") {
            value = object.GetAnimationBaseValue(property);
        }
    }
    return value;
}

void SetOrClearValue(wux::DependencyObject const& object,
                     wux::DependencyProperty const& property,
                     wf::IInspectable const& value, bool initial_apply) {
    if (IsBackgroundFill(object, property)) {
        auto it = t_delayed_fill.begin();
        for (; it != t_delayed_fill.end(); ++it) {
            if (auto live = it->first.get(); live && live == object) {
                break;
            }
        }
        if (value != wux::DependencyProperty::UnsetValue() && initial_apply &&
            it == t_delayed_fill.end()) {
            STYLER_LOG(LogLevel::Debug, L"deferring BackgroundFill.Fill");
            auto op = object.Dispatcher().TryRunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::High,
                [object, property, value]() {
                    // Runs later, on the dispatcher, well outside whatever
                    // ModifyingGuard scope the original SetOrClearValue call
                    // held (that one is long gone by now) - so this needs
                    // its own guard around SetValue itself, or the
                    // PropertyChanged callback it triggers looks like an
                    // external change and clobbers `original` with our own
                    // brush (review round 1, C2). The whole body, not just
                    // SetValue, sits inside one try: erase_if's
                    // weak_ref::get() and the captured objects' destructors
                    // ran unguarded before (review round 1, I1).
                    try {
                        {
                            ModifyingGuard guard;
                            object.SetValue(property, value);
                        }
                        std::erase_if(t_delayed_fill, [&](const auto& e) {
                            auto live = e.first.get();
                            return live && live == object;
                        });
                    } catch (winrt::hresult_error const& ex) {
                        STYLER_LOG(LogLevel::Error, L"deferred SetValue 0x%08X",
                                   static_cast<unsigned>(ex.code()));
                    } catch (...) {
                        STYLER_LOG(LogLevel::Error, L"deferred SetValue threw");
                    }
                });
            t_delayed_fill.push_back({winrt::make_weak(object), op});
            return;
        }
        if (it != t_delayed_fill.end()) {
            it->second.Cancel();
            t_delayed_fill.erase(it);
        }
    }

    if (value == wux::DependencyProperty::UnsetValue()) {
        object.ClearValue(property);
        return;
    }
    object.SetValue(property, value);
}

bool IsModifying() {
    return t_modifying;
}

// Shipped with a prev_ member (see the guard's declaration), restoring the
// PREVIOUS value here rather than hard-clearing to false: Task 6's
// CurrentStateChanged can fire synchronously while an outer SetValue is
// still in flight, and a nested guard clearing the flag out from under an
// outer one would reopen the exact bug this guard exists to close.
ModifyingGuard::ModifyingGuard() : prev_(t_modifying) {
    t_modifying = true;
}

ModifyingGuard::~ModifyingGuard() {
    t_modifying = prev_;
}

}  // namespace styler::tap
```

- [ ] **Step 4: `style_engine.h` — o contrato completo**

Substitua o header da Task 4 por este (mesmas três funções, mais o tema e a restauração):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <styler/matcher.h>
#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The prepared theme every thread applies. Immutable once set; replacing it
// does not touch elements already styled - callers restore first (see
// theme_session.cpp's reload). Null means "no theme".
void SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme);
std::shared_ptr<const styler::ResolvedTheme> CurrentTheme();

// Called from the standing subscription, on the reporting thread.
void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type);
void OnElementRemoved(ElementId id);

// Whether this thread's engine holds customization state for `id`.
bool ElementHasState(ElementId id);

// Restores every element this thread customized and forgets them. Must run
// on the owning UI thread, outside any XAML callback.
void RestoreAllOnThisThread();

// Counters for the log and for docs/smoke-test.md, this thread only.
struct EngineStats {
    size_t styled_elements = 0;
    size_t applied_properties = 0;
    size_t failed_styles = 0;
    size_t deferred_visual_state_styles = 0;  // Task 6 turns this to zero.
};
EngineStats StatsForThisThread();

}  // namespace styler::tap
```

- [ ] **Step 5: `style_engine.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_engine.h>

#include <atomic>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tap/log.h>
#include <tap/property_setter.h>

namespace styler::tap {
namespace {

// Heap-leaked like g_session: no namespace-scope destructor at detach.
auto* const g_theme =
    new std::atomic<std::shared_ptr<const styler::ResolvedTheme>>{nullptr};

// One customized property on one element.
struct PropertyState {
    wf::IInspectable original;      // What ReadLocalValue gave before us.
    wf::IInspectable custom;        // What we set (null when clearing).
    wf::IInspectable last_applied;  // ReadLocalValue right after our set.
    long long changed_token = 0;    // RegisterPropertyChangedCallback.
};

struct ElementState {
    winrt::weak_ref<wux::FrameworkElement> element;
    std::unordered_map<wux::DependencyProperty, PropertyState> properties;
};

thread_local std::unordered_map<ElementId, ElementState> t_state;
thread_local EngineStats t_stats;

// Per-thread cache of resolved setters, keyed by the immutable PreparedStyle
// the theme owns. Cleared when the theme changes. The value object (a brush,
// a transform) is shared by every element the style hits - upstream shares
// setter.Value() the same way.
thread_local std::unordered_map<const styler::PreparedStyle*, ResolvedSetter>
    t_setter_cache;
// A shared_ptr, not a raw ResolvedTheme*: holding a ref keeps the old
// theme's address from ever being reused, which a raw pointer cannot
// (review round 1, I2) - SetTheme(B) can free A, and B's allocation can
// legitimately land at A's old address, aliasing a stale PreparedStyle*
// cache key to the wrong style with no way to detect it.
thread_local std::shared_ptr<const styler::ResolvedTheme> t_cache_theme;

// ElementView over a live FrameworkElement (matcher.h documents the
// contract). Every method is called on the element's own UI thread.
class XamlElementView : public styler::ElementView {
public:
    XamlElementView(wux::FrameworkElement element, std::wstring reported)
        : element_(std::move(element)), reported_(std::move(reported)) {}

    std::wstring TypeName() const override {
        return std::wstring(winrt::get_class_name(element_));
    }
    std::wstring ReportedTypeName() const override { return reported_; }
    std::wstring Name() const override { return std::wstring(element_.Name()); }

    std::unique_ptr<styler::ElementView> Parent() const override {
        auto parent = wuxm::VisualTreeHelper::GetParent(element_)
                          .try_as<wux::FrameworkElement>();
        if (!parent) {
            return nullptr;
        }
        return std::make_unique<XamlElementView>(parent, std::wstring());
    }

    int IndexInParent() const override {
        auto parent = wuxm::VisualTreeHelper::GetParent(element_);
        if (!parent) {
            return -1;
        }
        int count = wuxm::VisualTreeHelper::GetChildrenCount(parent);
        for (int i = 0; i < count; ++i) {
            if (wuxm::VisualTreeHelper::GetChild(parent, i) == element_) {
                return i;
            }
        }
        return -1;
    }

    // Reads the local value and materializes `expected` through the same
    // <Setter> path the styles use, then compares as XAML would: primitives
    // unboxed (enums arrive as int32), anything else by reference.
    std::optional<bool> PropertyEquals(std::wstring_view property,
                                       std::wstring_view expected) const override {
        try {
            styler::PreparedStyle probe;
            probe.property = std::wstring(property);
            probe.value = std::wstring(expected);
            ResolvedSetter setter = ResolveSetter(TypeName(), reported_, probe);
            // ReadLocalValueWithWorkaround, not ReadLocalValue, like the
            // rest of the engine (review round 1 minor) - the same
            // BindingExpression case applies to a probed comparison too.
            wf::IInspectable actual = ReadLocalValueWithWorkaround(element_, setter.property);
            if (!actual || actual == wux::DependencyProperty::UnsetValue()) {
                return false;
            }
            auto a = actual.try_as<wf::IPropertyValue>();
            auto e = setter.value.try_as<wf::IPropertyValue>();
            if (!a || !e) {
                return actual == setter.value;
            }
            if (a.Type() == wf::PropertyType::String &&
                e.Type() == wf::PropertyType::String) {
                return a.GetString() == e.GetString();
            }
            if (a.Type() == wf::PropertyType::Boolean &&
                e.Type() == wf::PropertyType::Boolean) {
                return a.GetBoolean() == e.GetBoolean();
            }
            // Numbers and enums: compare as double; enums box as int32.
            auto as_number = [](wf::IPropertyValue const& v) -> std::optional<double> {
                switch (v.Type()) {
                    case wf::PropertyType::Double: return v.GetDouble();
                    case wf::PropertyType::Single: return v.GetSingle();
                    case wf::PropertyType::Int32: return v.GetInt32();
                    case wf::PropertyType::UInt32: return v.GetUInt32();
                    case wf::PropertyType::Int64: return static_cast<double>(v.GetInt64());
                    case wf::PropertyType::UInt64: return static_cast<double>(v.GetUInt64());
                    case wf::PropertyType::Int16: return v.GetInt16();
                    case wf::PropertyType::UInt16: return v.GetUInt16();
                    case wf::PropertyType::UInt8: return v.GetUInt8();
                    default: break;
                }
                if (auto i = v.try_as<int32_t>()) {  // Enums.
                    return *i;
                }
                return std::nullopt;
            };
            auto an = as_number(a);
            auto en = as_number(e);
            if (an && en) {
                return *an == *en;
            }
            return std::nullopt;
        } catch (winrt::hresult_error const&) {
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    wux::FrameworkElement element_;
    std::wstring reported_;
};

const ResolvedSetter* CachedSetter(
    const std::shared_ptr<const styler::ResolvedTheme>& theme,
    const styler::PreparedStyle& style, std::wstring_view type,
    std::wstring_view fallback) {
    if (t_cache_theme.get() != theme.get()) {
        t_setter_cache.clear();
        t_cache_theme = theme;
    }
    auto it = t_setter_cache.find(&style);
    if (it != t_setter_cache.end()) {
        return &it->second;
    }
    ResolvedSetter resolved = ResolveSetter(type, fallback, style);  // May throw.
    return &t_setter_cache.emplace(&style, std::move(resolved)).first->second;
}

void RestoreElement(ElementId id, ElementState& state) {
    auto element = state.element.get();
    for (auto& [property, prop] : state.properties) {
        if (!element) {
            break;
        }
        try {
            if (prop.changed_token) {
                element.UnregisterPropertyChangedCallback(property, prop.changed_token);
            }
            // Always calls through, even when `original` is null (never had
            // a local value before us): SetOrClearValue is also what
            // cancels a still-pending deferred BackgroundFill.Fill set
            // (review round 1 minor), and that must happen regardless of
            // whether there is anything to restore to. UnsetValue() there
            // means ClearValue - the correct outcome when there was no
            // original.
            ModifyingGuard guard;
            SetOrClearValue(element, property,
                            prop.original ? prop.original
                                          : wux::DependencyProperty::UnsetValue(),
                            false);
        } catch (winrt::hresult_error const& ex) {
            STYLER_LOG(LogLevel::Error, L"restore %llu failed 0x%08X",
                       static_cast<unsigned long long>(id),
                       static_cast<unsigned>(ex.code()));
        } catch (...) {
        }
    }
    state.properties.clear();
}

void ApplyProperty(ElementId id, wux::FrameworkElement const& element,
                   ElementState& state, wux::DependencyProperty const& property,
                   wf::IInspectable const& custom_or_unset) {
    PropertyState prop;
    prop.original = ReadLocalValueWithWorkaround(element, property);
    prop.custom = custom_or_unset;
    // A ModifyingGuard, not a hand-paired set/clear with no try/catch at
    // all: if SetOrClearValue throws (it can; it is documented to), the
    // flag must not stay stuck true for the rest of the thread's life -
    // every later PropertyChanged callback would return early at its own
    // IsModifying() check forever, and the shell's own overwrites would
    // never be captured as a new `original` again (review round 1, C1).
    // The guard's destructor runs during unwinding too, so this cannot
    // happen.
    {
        ModifyingGuard guard;
        SetOrClearValue(element, property, custom_or_unset, true);
    }
    prop.last_applied = ReadLocalValueWithWorkaround(element, property);

    // Something else (a Setter, a template) overwriting our value gets our
    // value back - and becomes the new original, so restore returns to what
    // the shell last wanted, not to what it wanted before we arrived.
    prop.changed_token = element.RegisterPropertyChangedCallback(
        property, [id](wux::DependencyObject const& sender,
                       wux::DependencyProperty const& changed) {
            try {
                if (IsModifying()) {
                    return;
                }
                auto it = t_state.find(id);
                if (it == t_state.end()) {
                    return;
                }
                auto pit = it->second.properties.find(changed);
                if (pit == it->second.properties.end()) {
                    return;
                }
                PropertyState& p = pit->second;
                wf::IInspectable local = ReadLocalValueWithWorkaround(sender, changed);
                if (local != p.last_applied) {
                    p.original = local;
                }
                {
                    ModifyingGuard guard;
                    SetOrClearValue(sender, changed,
                                    p.custom ? p.custom : wux::DependencyProperty::UnsetValue(),
                                    false);
                }
                p.last_applied = ReadLocalValueWithWorkaround(sender, changed);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        });
    state.properties[property] = std::move(prop);
    ++t_stats.applied_properties;
}

}  // namespace

void SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme) {
    g_theme->store(std::move(theme));
}

std::shared_ptr<const styler::ResolvedTheme> CurrentTheme() {
    return g_theme->load();
}

void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type) {
    std::shared_ptr<const styler::ResolvedTheme> theme = CurrentTheme();
    if (!theme) {
        return;
    }
    std::wstring reported = reported_type ? reported_type : L"";
    XamlElementView view(element, reported);
    std::vector<styler::RuleMatch> matches = styler::FindMatchingRules(*theme, view);
    if (matches.empty()) {
        return;
    }

    ElementState& state = t_state[id];
    if (!state.properties.empty()) {
        RestoreElement(id, state);  // Re-reported: start clean.
    }
    state.element = element;

    std::wstring type = view.TypeName();
    std::unordered_set<wux::DependencyProperty> claimed;
    for (const styler::RuleMatch& match : matches) {  // Last theme rule first.
        for (const styler::PreparedStyle& style : match.rule->styles) {
            if (!style.visual_state.empty() || match.vsg) {
                ++t_stats.deferred_visual_state_styles;  // Task 6.
                continue;
            }
            try {
                const ResolvedSetter* setter =
                    CachedSetter(theme, style, type, reported);
                if (!claimed.insert(setter->property).second) {
                    continue;  // An earlier (later-in-theme) rule owns it.
                }
                ApplyProperty(id, element, state, setter->property,
                              setter->clear ? wux::DependencyProperty::UnsetValue()
                                            : setter->value);
            } catch (winrt::hresult_error const& ex) {
                ++t_stats.failed_styles;
                STYLER_LOG(LogLevel::Error, L"rule %zu %s=%s on %s: 0x%08X",
                           match.rule->source_index, style.property.c_str(),
                           style.value.c_str(), type.c_str(),
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
                ++t_stats.failed_styles;
            }
        }
    }
    if (state.properties.empty()) {
        t_state.erase(id);
        return;
    }
    ++t_stats.styled_elements;
    STYLER_LOG(LogLevel::Debug, L"styled %s#%s: %zu properties", type.c_str(),
               view.Name().c_str(), state.properties.size());
}

void OnElementRemoved(ElementId id) {
    auto it = t_state.find(id);
    if (it == t_state.end()) {
        return;
    }
    RestoreElement(id, it->second);
    t_state.erase(id);
}

bool ElementHasState(ElementId id) {
    return id != ElementId::None && t_state.contains(id);
}

void RestoreAllOnThisThread() {
    std::vector<ElementId> ids;
    ids.reserve(t_state.size());
    for (const auto& [id, _] : t_state) {
        ids.push_back(id);
    }
    for (ElementId id : ids) {
        OnElementRemoved(id);
    }
    t_setter_cache.clear();
    t_cache_theme = nullptr;
    STYLER_LOG(LogLevel::Info, L"restored %zu elements on thread %lu", ids.size(),
               GetCurrentThreadId());
    t_stats = EngineStats{};
}

EngineStats StatsForThisThread() {
    return t_stats;
}

}  // namespace styler::tap
```

Dois pontos que o revisor vai olhar e que estão certos de propósito:

- `RestoreElement` itera `state.properties` enquanto `SetOrClearValue` pode disparar o `PropertyChanged` de outra propriedade do mesmo elemento. O callback só toca `t_state[id]` se encontrar a entrada e a propriedade — e `t_modifying` está `true` durante o `SetOrClearValue`, então ele retorna cedo. Não há reentrância que altere o mapa em iteração.
- O callback de `PropertyChanged` captura só `id` (um inteiro), nunca ponteiro para o estado: o estado mora no mapa `thread_local` e é procurado a cada disparo. Se o elemento já foi limpo, `find` falha e nada acontece.

- [ ] **Step 6: `theme_session.h/.cpp` — ler o config e carregar o tema**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>

namespace styler::tap {

// %APPDATA%\TaskbarStyler\config.json (spec section 4.2). Empty if APPDATA
// cannot be resolved.
std::wstring ConfigPath();

// Reads the config, loads <InitializationData()>\<theme>.json, prepares it
// and installs it with SetTheme. S_FALSE when no theme is configured (and
// SetTheme(nullptr) was applied). Fails closed: a malformed config or theme
// installs no theme and logs why (spec section 7.6).
HRESULT LoadConfiguredTheme();

}  // namespace styler::tap
```

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/theme_session.h>

#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <styler/config.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>
#include <tap/log.h>
#include <tap/style_engine.h>
#include <tap/visual_tree_watcher.h>

namespace styler::tap {
namespace {

// A theme id names a file under themes/; keep it to what the converter
// emits (letters, digits, '_', '&', '.', '-') so a config cannot point
// outside that directory.
bool ValidThemeId(const std::wstring& id) {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    for (wchar_t c : id) {
        bool ok = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'a' && c <= L'z') || c == L'_' || c == L'&' ||
                  c == L'-' || c == L'.';
        if (!ok) {
            return false;
        }
    }
    return id.find(L"..") == std::wstring::npos;
}

LogLevel ParseLevel(const std::wstring& s, LogLevel fallback) {
    if (s == L"error") return LogLevel::Error;
    if (s == L"info") return LogLevel::Info;
    if (s == L"debug") return LogLevel::Debug;
    return fallback;
}

}  // namespace

std::wstring ConfigPath() {
    wchar_t* appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        return L"";
    }
    std::wstring path(appdata);
    CoTaskMemFree(appdata);
    path += L"\\TaskbarStyler\\config.json";
    return path;
}

HRESULT LoadConfiguredTheme() {
    styler::Config config;
    std::wstring path = ConfigPath();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (in) {
            std::stringstream buf;
            buf << in.rdbuf();
            try {
                config = styler::ParseConfigJson(buf.str());
            } catch (const styler::ParseError& ex) {
                STYLER_LOG(LogLevel::Error, L"config.json invalid: %S", ex.what());
                SetTheme(nullptr);
                return E_INVALIDARG;
            }
        }
    }
    if (!config.log_level.empty()) {
        SetLogLevel(ParseLevel(config.log_level, GetLogLevel()));
    }
    if (config.theme.empty()) {
        SetTheme(nullptr);
        STYLER_LOG(LogLevel::Info, L"no theme configured");
        return S_FALSE;
    }
    if (!ValidThemeId(config.theme)) {
        STYLER_LOG(LogLevel::Error, L"theme id rejected: %s", config.theme.c_str());
        SetTheme(nullptr);
        return E_INVALIDARG;
    }
    std::wstring dir = InitializationData();
    if (dir.empty()) {
        STYLER_LOG(LogLevel::Error, L"no themes directory was passed at load");
        SetTheme(nullptr);
        return E_NOT_VALID_STATE;
    }
    std::filesystem::path file = std::filesystem::path(dir) / (config.theme + L".json");
    try {
        styler::Theme theme = styler::LoadThemeFromFile(file);
        auto prepared = std::make_shared<const styler::ResolvedTheme>(
            styler::PrepareTheme(theme));
        for (const auto& line : prepared->diagnostics) {
            STYLER_LOG(LogLevel::Error, L"%s", line.c_str());
        }
        STYLER_LOG(LogLevel::Info,
                   L"theme %s: %zu rules prepared, %d captures and %d dynamic "
                   L"values skipped, %d blur approximations",
                   prepared->id.c_str(), prepared->rules.size(),
                   prepared->skipped_captures, prepared->skipped_dynamic,
                   prepared->blur_approximations);
        SetTheme(prepared);
        return S_OK;
    } catch (const styler::ParseError& ex) {
        STYLER_LOG(LogLevel::Error, L"theme %s rejected: %S", config.theme.c_str(),
                   ex.what());
    } catch (const std::exception& ex) {
        STYLER_LOG(LogLevel::Error, L"theme %s failed: %S", config.theme.c_str(),
                   ex.what());
    }
    SetTheme(nullptr);
    return E_FAIL;
}

}  // namespace styler::tap
```

`theme_session.cpp` linka `styler_core` (o alvo MODULE precisa de `target_link_libraries(taskbar_styler_tap PRIVATE styler_core)`) e `shell32.lib`/`ole32.lib` para `SHGetKnownFolderPath`/`CoTaskMemFree`.

- [ ] **Step 7: Ligar no `SetSite`**

Em `tap_boundary.cpp`, incluir `<tap/theme_session.h>` e chamar `LoadConfiguredTheme()` **antes** de `StartSubscription()` — o lote inicial da assinatura é o que aplica o tema a tudo que já existe:

```cpp
                HRESULT theme_hr = LoadConfiguredTheme();
                if (FAILED(theme_hr)) {
                    STYLER_LOG(LogLevel::Error, L"LoadConfiguredTheme failed 0x%08X",
                               static_cast<unsigned>(theme_hr));
                }
                HRESULT sub_hr = StartSubscription();
                ...
                EngineStats stats = StatsForThisThread();
                STYLER_LOG(LogLevel::Info,
                           L"initial apply: %zu elements, %zu properties, %zu failed, "
                           L"%zu visual-state styles deferred",
                           stats.styled_elements, stats.applied_properties,
                           stats.failed_styles, stats.deferred_visual_state_styles);
```

- [ ] **Step 8: Build, testes e smoke — a entrega**

Crie `%APPDATA%\TaskbarStyler\config.json` à mão:

```json
{ "theme": "TranslucentTaskbar", "logLevel": "info" }
```

Reinicie o Explorer, `cmake --build build`, `ctest --test-dir build --output-on-failure`, `build\src\cli\taskbar-styler.exe load`.

Esperado, visível: a linha `Rectangle#BackgroundStroke` some (regra `Visibility=Collapsed`) e o fundo da taskbar muda para o `AcrylicBrush` interino (translúcido se o acrílico renderizar dentro da ilha; um tom sólido de `#25323232` se cair no fallback — **diga qual dos dois você viu**, é o dado que decide o Plano 3b). No log: `theme TranslucentTaskbar: N rules prepared …`, `initial apply: E elements, P properties, 0 failed, V visual-state styles deferred`. Depois troque o config para `"theme": ""`, reinicie o Explorer e `load`: `no theme configured`, taskbar padrão. Anote E, P e V no relatório.

Se `failed` for maior que zero, cada falha tem uma linha `rule <i> Prop=valor on Type: 0x…` — copie as três primeiras para o relatório.

- [ ] **Step 9: Commit**

```bash
git add src/core tests/core src/tap
git commit -m "feat(tap): motor de estilo - aplica e restaura propriedades via Setter/XamlReader"
```

---

### Task 6: TAP — estados visuais

842 estilos em 38 temas só valem num estado visual (`Background@ActivePointerOver=…`), e 236 alvos nomeiam o grupo (`Taskbar.TaskListButton@CommonStates > …`). O upstream agrupa as propriedades por `VisualStateGroup`, aplica o valor do estado atual (ou o incondicional), e reaplica em `CurrentStateChanged` (`vendor:17322-17470`; restauração em `:17554-17625`). Esta task faz o mesmo e zera o contador `deferred_visual_state_styles` da Task 5.

**Files:**
- Modify: `src/tap/style_engine.cpp` (só ele; o header não muda)

**Interfaces:**
- Consumes: `RuleMatch::vsg` (`VisualStateGroupRef{name, ancestor_depth}`, Task 3); `PreparedStyle::visual_state` (Task 3); tudo da Task 5.
- Produces: nada novo público. `StatsForThisThread().deferred_visual_state_styles` passa a ser sempre `0`.

**Semântica, copiada do upstream:**
- Os estilos de um elemento são agrupados em **baldes** por grupo de estados: o balde "sem grupo" recebe os estilos incondicionais de matches sem `@VSG`; um match com `@VSG` põe **todos** os seus estilos (com e sem `@State`) no balde daquele grupo, resolvido no elemento `ancestor_depth` pais acima.
- Dentro de um balde, cada propriedade tem um mapa `estado → valor`, onde a chave vazia é o valor incondicional. Ao aplicar: valor do estado atual; senão o incondicional; senão nada por enquanto.
- Um estilo `Prop@Estado=…` num match **sem** grupo nunca fica ativo (o upstream procura o estado atual num grupo nulo e cai no incondicional). É pulado com uma linha em `Debug`.
- Uma propriedade só pertence a um balde: a primeira regra (na ordem que `FindMatchingRules` devolve) que a reivindica fica com ela, em qualquer balde.
- `CurrentStateChanged`: para cada propriedade do balde, valor do novo estado, senão incondicional; se havia valor aplicado e agora não há, restaura o original.
- `GetVisualStateGroup` tem dois desvios de crash do upstream que **precisam** vir junto: `Taskbar.TaskListButtonPanel` filho de `Taskbar.SearchBoxLaunchListButton` e `SearchUx.SearchUI.SearchButtonRootGrid` filho de `SearchUx.SearchUI.SearchPillButton` devolvem grupo nulo (`vendor:15683-15720`) — acessar o primeiro item da lista de grupos deles derruba o explorer.

- [ ] **Step 1: O modelo de estado por elemento muda**

Em `style_engine.cpp`, substitua `PropertyState`/`ElementState` por:

```cpp
struct PropertyState {
    // Value per visual state name; "" is the unconditional value. A null
    // IInspectable means "clear the property" (Prop:= with empty value).
    std::map<std::wstring, wf::IInspectable> values;
    bool applied = false;           // We currently hold a custom value.
    wf::IInspectable original;      // Valid while `applied`.
    wf::IInspectable last_applied;  // ReadLocalValue after our last set.
    long long changed_token = 0;
};

struct VsgBucket {
    // Shipped as a STRONG ref, not weak_ref as drafted above: measured (Task
    // 6 smoke test) that a weak_ref here never resolves back - group.get()
    // came back null every time OnElementAdded's second pass reached it a
    // few lines later, so RegisterStateWatch never subscribed and
    // CurrentStateChanged never fired. Dropped on all four teardown paths
    // (OnElementRemoved, the re-report branch, RestoreAllOnThisThread,
    // t_state's own destruction at thread exit), so it does not pin the
    // element.
    wux::VisualStateGroup group{nullptr};  // Empty: unconditional.
    long long state_changed_token = 0;
    std::unordered_map<wux::DependencyProperty, PropertyState, DependencyPropertyHash>
        properties;
};

struct ElementState {
    winrt::weak_ref<wux::FrameworkElement> element;
    // A list: the CurrentStateChanged handler captures the bucket index and
    // buckets are never reordered or erased individually.
    std::vector<VsgBucket> buckets;
};
```

`ElementHasState`, `RestoreAllOnThisThread` e `OnElementRemoved` continuam iguais; `RestoreElement` e `OnElementAdded` mudam abaixo.

- [ ] **Step 2: `GetVisualStateGroup` e a resolução do ancestral**

No namespace anônimo:

```cpp
wux::VisualStateGroup GetVisualStateGroup(wux::FrameworkElement const& element,
                                          std::wstring_view name) {
    // Two elements whose group list reports one item that crashes on
    // access (upstream vendor:15683-15720). Skip them outright.
    auto cls = winrt::get_class_name(element);
    auto parent_is = [&](std::wstring_view parent_cls) {
        auto parent = wuxm::VisualTreeHelper::GetParent(element)
                          .try_as<wux::FrameworkElement>();
        return parent && winrt::get_class_name(parent) == parent_cls;
    };
    if (cls == L"Taskbar.TaskListButtonPanel" &&
        parent_is(L"Taskbar.SearchBoxLaunchListButton")) {
        return nullptr;
    }
    if (cls == L"SearchUx.SearchUI.SearchButtonRootGrid" &&
        parent_is(L"SearchUx.SearchUI.SearchPillButton")) {
        return nullptr;
    }
    for (auto const& group : wux::VisualStateManager::GetVisualStateGroups(element)) {
        if (group.Name() == name) {
            return group;
        }
    }
    return nullptr;
}

// The element `depth` parents above `leaf`, or null if the tree is shorter.
wux::FrameworkElement AncestorAt(wux::FrameworkElement leaf, int depth) {
    wux::FrameworkElement cur = leaf;
    for (int i = 0; i < depth && cur; ++i) {
        cur = wuxm::VisualTreeHelper::GetParent(cur).try_as<wux::FrameworkElement>();
    }
    return cur;
}

std::wstring CurrentStateName(wux::VisualStateGroup const& group) {
    if (!group) {
        return L"";
    }
    auto state = group.CurrentState();
    return state ? std::wstring(state.Name()) : L"";
}

// Value for `state`, else the unconditional one. Returns whether one exists.
bool PickValue(const PropertyState& prop, const std::wstring& state,
               wf::IInspectable* out) {
    auto it = prop.values.find(state);
    if (it == prop.values.end() && !state.empty()) {
        it = prop.values.find(L"");
    }
    if (it == prop.values.end()) {
        return false;
    }
    *out = it->second;
    return true;
}
```

- [ ] **Step 3: Aplicar uma propriedade e reagir à mudança de estado**

`ApplyProperty` da Task 5 vira duas funções: `SetCustom` (aplica um valor, capturando o original se ainda não aplicado) e `Unapply` (restaura). O callback de `PropertyChanged` fica igual, mas relê `values` pelo estado atual do balde:

```cpp
wf::IInspectable OrUnset(wf::IInspectable const& v) {
    return v ? v : wux::DependencyProperty::UnsetValue();
}

void SetCustom(wux::FrameworkElement const& element,
               wux::DependencyProperty const& property, PropertyState& prop,
               wf::IInspectable const& value, bool initial) {
    if (!prop.applied) {
        prop.original = ReadLocalValueWithWorkaround(element, property);
        prop.applied = true;
    }
    t_modifying = true;
    SetOrClearValue(element, property, OrUnset(value), initial);
    t_modifying = false;
    prop.last_applied = ReadLocalValueWithWorkaround(element, property);
}

void Unapply(wux::FrameworkElement const& element,
             wux::DependencyProperty const& property, PropertyState& prop) {
    if (!prop.applied) {
        return;
    }
    t_modifying = true;
    SetOrClearValue(element, property, OrUnset(prop.original), false);
    t_modifying = false;
    prop.applied = false;
    prop.original = nullptr;
}

// Re-evaluates one bucket against `state_name`: apply, re-apply or restore
// each property. Runs on state change and on first apply.
void ApplyBucketForState(ElementId id, wux::FrameworkElement const& element,
                         VsgBucket& bucket, const std::wstring& state_name,
                         bool initial) {
    for (auto& [property, prop] : bucket.properties) {
        try {
            wf::IInspectable value;
            if (PickValue(prop, state_name, &value)) {
                SetCustom(element, property, prop, value, initial);
            } else {
                Unapply(element, property, prop);
            }
        } catch (winrt::hresult_error const& ex) {
            t_modifying = false;
            ++t_stats.failed_styles;
            STYLER_LOG(LogLevel::Error, L"apply %llu state '%s' failed 0x%08X",
                       static_cast<unsigned long long>(id), state_name.c_str(),
                       static_cast<unsigned>(ex.code()));
        } catch (...) {
            t_modifying = false;
            ++t_stats.failed_styles;
        }
    }
}

void RegisterPropertyWatch(ElementId id, size_t bucket_index,
                           wux::FrameworkElement const& element,
                           wux::DependencyProperty const& property,
                           PropertyState& prop) {
    prop.changed_token = element.RegisterPropertyChangedCallback(
        property, [id, bucket_index](wux::DependencyObject const& sender,
                                     wux::DependencyProperty const& changed) {
            try {
                if (t_modifying) {
                    return;
                }
                auto it = t_state.find(id);
                if (it == t_state.end() || bucket_index >= it->second.buckets.size()) {
                    return;
                }
                VsgBucket& bucket = it->second.buckets[bucket_index];
                auto pit = bucket.properties.find(changed);
                if (pit == bucket.properties.end() || !pit->second.applied) {
                    return;
                }
                PropertyState& p = pit->second;
                wf::IInspectable local = ReadLocalValueWithWorkaround(sender, changed);
                if (local != p.last_applied) {
                    p.original = local;  // The shell changed its mind; honour it on restore.
                }
                wf::IInspectable value;
                if (!PickValue(p, CurrentStateName(bucket.group.get()), &value)) {
                    return;
                }
                auto element = sender.try_as<wux::FrameworkElement>();
                if (!element) {
                    return;
                }
                t_modifying = true;
                SetOrClearValue(element, changed, OrUnset(value), false);
                p.last_applied = ReadLocalValueWithWorkaround(element, changed);
                t_modifying = false;
            } catch (winrt::hresult_error const&) {
                t_modifying = false;
            } catch (...) {
                t_modifying = false;
            }
        });
}

void RegisterStateWatch(ElementId id, size_t bucket_index,
                        wux::VisualStateGroup const& group, VsgBucket& bucket) {
    bucket.state_changed_token = group.CurrentStateChanged(
        [id, bucket_index](wf::IInspectable const&,
                           wux::VisualStateChangedEventArgs const& e) {
            try {
                auto it = t_state.find(id);
                if (it == t_state.end() || bucket_index >= it->second.buckets.size()) {
                    return;
                }
                auto element = it->second.element.get();
                if (!element) {
                    return;
                }
                auto new_state = e.NewState();
                std::wstring name = new_state ? std::wstring(new_state.Name()) : L"";
                ApplyBucketForState(id, element, it->second.buckets[bucket_index],
                                    name, false);
            } catch (winrt::hresult_error const&) {
                t_modifying = false;
            } catch (...) {
                t_modifying = false;
            }
        });
}
```

- [ ] **Step 4: `OnElementAdded` monta os baldes; `RestoreElement` desmonta**

```cpp
void RestoreElement(ElementId id, ElementState& state) {
    auto element = state.element.get();
    for (VsgBucket& bucket : state.buckets) {
        if (auto group = bucket.group.get(); group && bucket.state_changed_token) {
            try {
                group.CurrentStateChanged(winrt::event_token{bucket.state_changed_token});
            } catch (...) {
            }
        }
        for (auto& [property, prop] : bucket.properties) {
            if (!element) {
                break;
            }
            try {
                if (prop.changed_token) {
                    element.UnregisterPropertyChangedCallback(property, prop.changed_token);
                }
                Unapply(element, property, prop);
            } catch (winrt::hresult_error const& ex) {
                t_modifying = false;
                STYLER_LOG(LogLevel::Error, L"restore %llu failed 0x%08X",
                           static_cast<unsigned long long>(id),
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
                t_modifying = false;
            }
        }
    }
    state.buckets.clear();
}
```

Em `OnElementAdded`, o laço de estilos passa a distribuir por balde em vez de aplicar direto:

```cpp
    ElementState& state = t_state[id];
    if (!state.buckets.empty()) {
        RestoreElement(id, state);
    }
    state.element = element;

    std::wstring type = view.TypeName();
    std::unordered_set<wux::DependencyProperty> claimed;
    for (const styler::RuleMatch& match : matches) {
        // Resolve this match's bucket: the group on the ancestor the
        // selector named, or the unconditional bucket.
        wux::VisualStateGroup group{nullptr};
        if (match.vsg) {
            try {
                if (auto owner = AncestorAt(element, match.vsg->ancestor_depth)) {
                    group = GetVisualStateGroup(owner, match.vsg->name);
                }
            } catch (winrt::hresult_error const&) {
            }
            if (!group) {
                STYLER_LOG(LogLevel::Debug, L"rule %zu: group %s not found on %s",
                           match.rule->source_index, match.vsg->name.c_str(),
                           type.c_str());
            }
        }
        size_t bucket_index = state.buckets.size();
        for (size_t i = 0; i < state.buckets.size(); ++i) {
            auto existing = state.buckets[i].group.get();
            if ((!group && !existing) || (group && existing == group)) {
                bucket_index = i;
                break;
            }
        }
        if (bucket_index == state.buckets.size()) {
            VsgBucket b;
            if (group) {
                b.group = group;
            }
            state.buckets.push_back(std::move(b));
        }
        VsgBucket& bucket = state.buckets[bucket_index];

        // Shipped with a per-MATCH claim set (claimed_by_match below),
        // merged into `claimed` only once the match finishes - not the bare
        // `claimed.insert(...)` per style line drafted above. That version
        // would make a single rule's own Background@ActiveNormal=... and
        // Background@ActivePointerOver=... fight each other for the same
        // property, since the SECOND style line would find the property
        // already claimed by the rule's OWN first line. Upstream's
        // propertiesAdded (vendor:15933-16031) inserts once per
        // (rule, property) AFTER a rule's own states are merged into one
        // per-property entry - never once per style line - which is what
        // the per-match set restores. A group-less Prop@State is claimed
        // too (vendor:16022 inserts before group resolution is even
        // consulted), just before it is found inert below.
        std::unordered_set<wux::DependencyProperty, DependencyPropertyHash>
            claimed_by_match;
        for (const styler::PreparedStyle& style : match.rule->styles) {
            try {
                const ResolvedSetter* setter = CachedSetter(theme, style, type, reported);
                if (claimed.contains(setter->property)) {
                    continue;  // An earlier (later-in-theme) match owns it.
                }
                claimed_by_match.insert(setter->property);
                if (!style.visual_state.empty() && !group) {
                    STYLER_LOG(LogLevel::Debug, L"rule %zu: %s@%s without a group, inert",
                               match.rule->source_index, style.property.c_str(),
                               style.visual_state.c_str());
                    continue;
                }
                PropertyState& prop = bucket.properties[setter->property];
                prop.values[style.visual_state] = setter->clear ? nullptr : setter->value;
            } catch (winrt::hresult_error const& ex) {
                ++t_stats.failed_styles;
                STYLER_LOG(LogLevel::Error, L"rule %zu %s=%s on %s: 0x%08X",
                           match.rule->source_index, style.property.c_str(),
                           style.value.c_str(), type.c_str(),
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
                ++t_stats.failed_styles;
            }
        }
        claimed.insert(claimed_by_match.begin(), claimed_by_match.end());
    }

    // Drop empty buckets, then apply each for its current state and watch.
    std::erase_if(state.buckets, [](const VsgBucket& b) { return b.properties.empty(); });
    if (state.buckets.empty()) {
        t_state.erase(id);
        return;
    }
    size_t applied = 0;
    for (size_t i = 0; i < state.buckets.size(); ++i) {
        VsgBucket& bucket = state.buckets[i];
        auto group = bucket.group.get();
        ApplyBucketForState(id, element, bucket, CurrentStateName(group), true);
        for (auto& [property, prop] : bucket.properties) {
            try {
                RegisterPropertyWatch(id, i, element, property, prop);
            } catch (winrt::hresult_error const&) {
            }
            applied += prop.applied ? 1 : 0;
        }
        if (group) {
            try {
                RegisterStateWatch(id, i, group, bucket);
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"CurrentStateChanged subscribe 0x%08X",
                           static_cast<unsigned>(ex.code()));
            }
        }
    }
    t_stats.applied_properties += applied;
    ++t_stats.styled_elements;
```

Atenção à ordem: um estilo `Prop@Estado` reivindica a propriedade tanto quanto um incondicional — é assim que o upstream faz (`propertiesAdded` é por `DependencyProperty`, não por estado). Uma regra posterior com `Background=…` e uma anterior com `Background@PointerOver=…` na **mesma** propriedade: a posterior (que vem primeiro) vence e a anterior nunca aplica. Isso é fidelidade, não bug; deixe um comentário dizendo isso.

- [ ] **Step 5: Build e smoke**

Escolha um tema da lista dos 38 com `@VisualState` que tenha `ActivePointerOver` (por exemplo `Lucent`, `Bubbles` ou `RosePine` — confira em `themes/*.json`), configure em `config.json`, reinicie o Explorer, `load`.

- `initial apply: … 0 visual-state styles deferred`.
- Passe o mouse sobre o botão de um app aberto: o visual muda conforme o tema e volta ao sair. Com `logLevel: debug`, cada troca gera uma linha `apply <id> state 'ActivePointerOver'` seguida de outra para `ActiveNormal`.
- `released so far` continua estabilizando entre rajadas (o estado por estado visual não muda a contabilidade de handles).

Anote no relatório qual tema usou, o nome dos estados vistos no log, e se algum `failed` apareceu.

- [ ] **Step 6: Commit**

```bash
git add src/tap/style_engine.cpp
git commit -m "feat(tap): estilos por estado visual com reaplicacao em CurrentStateChanged"
```

---

### Task 7: Config, sinal de recarga e o CLI `apply` / `reset` / `list` / `status`

A fronteira entre processos do §4.2, exatamente como a spec desenha: um arquivo e um Event nomeado, numa direção só. Trocar de tema sem reiniciar o explorer é o critério 1 do §2 ("em tempo real"). A recarga é **desfazer → desassinar → recarregar tema → reassinar**: o lote inicial da nova assinatura reaplica tudo, incluindo elementos que o tema antigo não tocava — sem precisar de uma travessia própria.

**Files:**
- Create: `src/tap/ipc.h`
- Modify: `src/tap/theme_session.h`, `src/tap/theme_session.cpp`, `src/tap/tap_boundary.cpp`, `src/cli/main.cpp`, `src/cli/CMakeLists.txt`

**Interfaces:**
- Consumes: `SerializeConfigJson`/`ParseConfigJson` (Task 5), `LoadConfiguredTheme`, `RestoreAllOnThisThread`, `Start/StopSubscription`, `RunOnWindowThread`, `GetTaskbarUiWnd`, `GetXamlHostWnds` (Plano 2), `Theme::os_feature_variant` (Plano 1).
- Produces:
  - `ipc.h`: `constexpr wchar_t kReloadEventName[] = L"Local\\TaskbarStyler.Reload";` e `inline std::wstring ConfigPath()` (sai de `theme_session`, para o CLI usar o mesmo).
  - `HRESULT StartReloadWatch()` / `void StopReloadWatch()` — o TAP cria o Event (auto-reset) e espera nele numa thread de pool.
  - `void ReloadThemeOnUiThread()` — a sequência de recarga; roda na thread de UI da taskbar.
  - CLI: `apply <ThemeId>`, `reset`, `list`, `status` (estendido), `setup` (Task 4 — grava `DisableCompositionDiag=1` elevado).
  - `LoadConfiguredTheme` passa a honrar `osFeatureVariant` (Squircle).

- [ ] **Step 1: `ipc.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <string>

// Shared between the TAP (inside explorer) and the CLI/Tray. Nothing here
// links to either side: header-only on purpose.
namespace styler::tap {

// Auto-reset event. The TAP creates it at SetSite and waits on it; a writer
// sets it after rewriting config.json. Local\ scopes it to the session.
constexpr wchar_t kReloadEventName[] = L"Local\\TaskbarStyler.Reload";

// %APPDATA%\TaskbarStyler\config.json (spec section 4.2). Empty when the
// folder cannot be resolved.
inline std::wstring ConfigPath() {
    wchar_t* appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                    &appdata))) {
        return L"";
    }
    std::wstring path(appdata);
    CoTaskMemFree(appdata);
    path += L"\\TaskbarStyler\\config.json";
    return path;
}

}  // namespace styler::tap
```

Remova o `ConfigPath` de `theme_session.h/.cpp` e inclua `<tap/ipc.h>` lá.

- [ ] **Step 2: A variante por feature do SO (Squircle)**

Em `theme_session.cpp`, no namespace anônimo — cópia do upstream `vendor:IsOsFeatureEnabled`, uma **consulta** a `ntdll` por `GetProcAddress`, sem escrever em processo algum:

```cpp
std::optional<bool> IsOsFeatureEnabled(std::uint32_t feature_id) {
    enum FEATURE_ENABLED_STATE {
        FEATURE_ENABLED_STATE_DEFAULT = 0,
        FEATURE_ENABLED_STATE_DISABLED = 1,
        FEATURE_ENABLED_STATE_ENABLED = 2,
    };
#pragma pack(push, 1)
    struct RTL_FEATURE_CONFIGURATION {
        unsigned int featureId;
        unsigned __int32 group : 4;
        FEATURE_ENABLED_STATE enabledState : 2;
        unsigned __int32 enabledStateOptions : 1;
        unsigned __int32 unused1 : 1;
        unsigned __int32 variant : 6;
        unsigned __int32 variantPayloadKind : 2;
        unsigned __int32 unused2 : 16;
        unsigned int payload;
    };
#pragma pack(pop)
    using Fn = int(NTAPI*)(UINT32, int, INT64*, RTL_FEATURE_CONFIGURATION*);
    static Fn query = []() -> Fn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll ? reinterpret_cast<Fn>(
                           GetProcAddress(ntdll, "RtlQueryFeatureConfiguration"))
                     : nullptr;
    }();
    if (!query) {
        return std::nullopt;
    }
    RTL_FEATURE_CONFIGURATION feature{};
    INT64 change_stamp = 0;
    if (query(feature_id, 1, &change_stamp, &feature) < 0) {
        return std::nullopt;
    }
    switch (feature.enabledState) {
        case FEATURE_ENABLED_STATE_DISABLED: return false;
        case FEATURE_ENABLED_STATE_ENABLED: return true;
        default: return std::nullopt;
    }
}
```

E em `LoadConfiguredTheme`, depois de `LoadThemeFromFile` e antes de `PrepareTheme`:

```cpp
        if (theme.os_feature_variant) {
            const auto& v = *theme.os_feature_variant;
            if (IsOsFeatureEnabled(v.feature_id).value_or(false) &&
                ValidThemeId(v.theme_id)) {
                STYLER_LOG(LogLevel::Info, L"feature %u on: using variant %s",
                           v.feature_id, v.theme_id.c_str());
                theme = styler::LoadThemeFromFile(
                    std::filesystem::path(dir) / (v.theme_id + L".json"));
            }
        }
```

- [ ] **Step 3: A recarga**

`theme_session.h` ganha:

```cpp
// Creates kReloadEventName and waits on it from a thread-pool thread. Each
// signal runs ReloadThemeOnUiThread on the taskbar UI thread. Idempotent.
HRESULT StartReloadWatch();
void StopReloadWatch();

// Restore on every initialized host thread, drop the subscription, reload
// the configured theme, re-subscribe (the fresh initial flood re-applies to
// everything). Must run on the taskbar UI thread - the thread SetSite ran
// on - because the flood lands on the caller and only initialized threads
// apply styles.
void ReloadThemeOnUiThread();
```

`theme_session.cpp`:

```cpp
#include <tap/change_subscription.h>
#include <tap/ipc.h>
#include <tap/thread_init.h>

namespace {

void WINAPI RestoreThunk(void*) {
    RestoreAllOnThisThread();
}

void WINAPI ReloadThunk(void*) {
    ReloadThemeOnUiThread();
}

struct ReloadWatch {
    HANDLE event = nullptr;
    HANDLE wait = nullptr;
};
// Heap-leaked like g_session (Plano 2, Ruling 11).
auto* const g_reload = new std::atomic<ReloadWatch*>{nullptr};

void CALLBACK OnReloadSignaled(void*, BOOLEAN) {
    // Thread-pool thread: only hop to the UI thread here.
    try {
        HWND ui = GetTaskbarUiWnd();
        if (!ui || !RunOnWindowThread(ui, ReloadThunk, nullptr)) {
            STYLER_LOG(LogLevel::Error, L"reload: taskbar UI window not found");
        }
    } catch (...) {
    }
}

}  // namespace

void ReloadThemeOnUiThread() {
    try {
        STYLER_LOG(LogLevel::Info, L"reload requested");
        // 1. Restore, on every thread that may hold state. This thread first
        //    (direct call), then each host through its own message loop.
        RestoreAllOnThisThread();
        for (HWND host : GetXamlHostWnds()) {
            if (GetWindowThreadProcessId(host, nullptr) != GetCurrentThreadId()) {
                RunOnWindowThread(host, RestoreThunk, nullptr);
            }
        }
        // 2. Drop the subscription so the re-advise below re-floods.
        StopSubscription();
        // 3. New theme (or none), then re-subscribe: the initial flood on this
        //    thread applies it to everything already on screen.
        LoadConfiguredTheme();
        HRESULT hr = StartSubscription();
        if (FAILED(hr)) {
            STYLER_LOG(LogLevel::Error, L"reload: StartSubscription 0x%08X",
                       static_cast<unsigned>(hr));
        }
        EngineStats stats = StatsForThisThread();
        STYLER_LOG(LogLevel::Info, L"reload applied: %zu elements, %zu properties, %zu failed",
                   stats.styled_elements, stats.applied_properties, stats.failed_styles);
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"reload threw");
    }
}

HRESULT StartReloadWatch() {
    if (g_reload->load()) {
        return S_FALSE;
    }
    auto* watch = new (std::nothrow) ReloadWatch{};
    if (!watch) {
        return E_OUTOFMEMORY;
    }
    watch->event = CreateEventW(nullptr, FALSE, FALSE, kReloadEventName);
    if (!watch->event) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        delete watch;
        return hr;
    }
    if (!RegisterWaitForSingleObject(&watch->wait, watch->event, OnReloadSignaled,
                                     nullptr, INFINITE, WT_EXECUTEDEFAULT)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(watch->event);
        delete watch;
        return hr;
    }
    ReloadWatch* expected = nullptr;
    if (!g_reload->compare_exchange_strong(expected, watch)) {
        UnregisterWaitEx(watch->wait, INVALID_HANDLE_VALUE);
        CloseHandle(watch->event);
        delete watch;
        return S_FALSE;
    }
    STYLER_LOG(LogLevel::Info, L"reload watch started");
    return S_OK;
}

void StopReloadWatch() {
    ReloadWatch* watch = g_reload->exchange(nullptr);
    if (!watch) {
        return;
    }
    UnregisterWaitEx(watch->wait, INVALID_HANDLE_VALUE);  // Waits for a running callback.
    CloseHandle(watch->event);
    delete watch;
}
```

No `SetSite`: `StartReloadWatch()` logo depois de `StartSubscription()`; no ramo `!site`, `StopReloadWatch()` **antes** de `StopSubscription()`, para nenhuma recarga entrar no meio do desligamento.

Sobre a espera de `UnregisterWaitEx` com `INVALID_HANDLE_VALUE` de dentro de `SetSite(nullptr)`: se um `OnReloadSignaled` estiver rodando e bloqueado num `SendMessage` para a thread que está em `SetSite`, é deadlock. Esse caminho não é atingido por nada vivo hoje (Plano 2 documenta), e o `SendMessage` cross-thread despacha mensagens enviadas enquanto espera, o que desfaz o nó na prática. Registre como risco conhecido no relatório; não "conserte" com timeouts arbitrários.

- [ ] **Step 4: O CLI**

`src/cli/CMakeLists.txt`: `target_link_libraries(taskbar_styler_cli PRIVATE styler_core)` (para `config.h`), mais `shell32.lib` e `ole32.lib`.

`src/cli/main.cpp` ganha, no namespace anônimo:

```cpp
#include <fstream>
#include <sstream>
#include <filesystem>

#include <styler/config.h>
#include <styler/selector.h>
#include <tap/ipc.h>

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
```

`CmdStatus` (existente) passa a imprimir também `tema configurado: <id ou nenhum>` a partir de `ReadConfig()` e `TAP: carregado|nao carregado` a partir de `OpenEventW(SYNCHRONIZE, …)` (abre e fecha, sem sinalizar). `Usage()` e o despacho em `wmain` ganham as três entradas; `apply` recebe `argv[2]`.

- [ ] **Step 5: Build e smoke — trocar de tema ao vivo**

Reinicie o Explorer e `cmake --build build`. Então, **sem reiniciar o Explorer entre os passos**:

```
build\src\cli\taskbar-styler.exe list            # 55 temas
build\src\cli\taskbar-styler.exe apply TranslucentTaskbar   # carrega o TAP
build\src\cli\taskbar-styler.exe status
build\src\cli\taskbar-styler.exe apply Lucent    # troca ao vivo
build\src\cli\taskbar-styler.exe reset           # taskbar padrao
build\src\cli\taskbar-styler.exe apply Aeris
```

Esperado no log, na segunda `apply`: `reload requested`, `restored N elements on thread …` (uma linha por thread com estado), `subscription stopped`, `theme Lucent: …`, `subscription started`, `reload applied: …`. A taskbar muda visivelmente a cada comando, e `reset` a devolve ao padrão — **confira que o `BackgroundStroke` volta a aparecer**, é a prova de que o original foi restaurado e não só sobrescrito.

Depois de três trocas, o `released so far` deve estabilizar de novo: cada recarga gera um lote novo (e portanto liberações novas); o que não pode acontecer é o número continuar subindo com a taskbar parada. Anote os valores antes e depois de cada `apply`.

- [ ] **Step 6: Commit**

```bash
git add src/tap src/cli
git commit -m "feat: config.json + Event de recarga; CLI apply/reset/list/status"
```

---

### Task 8: Variáveis de recurso, checklist de smoke e CI

Três temas (`Blob`, `LayerMicaUI`, `Pills` — 24 entradas) trocam recursos do `ResourceDictionary` da aplicação em vez de propriedades de elementos: `AdaptiveIndicator@Light=#000000`, `Accent1@Dark={ThemeResource SystemAccentColorLight3}`. O parsing das chaves é puro e vai para o core; a mesclagem no dicionário da thread vai para o TAP, espelhando `vendor:19085-19175` e `:19249-19300`. Depois, a checklist manual e o CI fecham o plano.

**Files:**
- Create: `src/core/include/styler/resource_variables.h`, `src/core/resource_variables.cpp`, `tests/core/test_resource_variables.cpp`, `src/tap/resource_variables.h`, `src/tap/resource_variables.cpp`
- Modify: `src/tap/winrt_common.h` (dois includes), `src/tap/style_engine.cpp` (mesclar na primeira adição por thread; desmesclar em `RestoreAllOnThisThread`), `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `src/tap/CMakeLists.txt`, `docs/smoke-test.md`, `README.md`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `ResolvedTheme::resource_variables` (Task 3, constantes já aplicadas); `CurrentTheme()`, `RestoreAllOnThisThread()` (Task 5).
- Produces:
  - core: `enum class ResourceTheme { None, Dark, Light }; enum class ResourceValueType { String, Xaml, ThemeResourceReference }; struct ResourceVariable { std::wstring key, value; ResourceTheme theme; ResourceValueType type; }; std::vector<ResourceVariable> ParseResourceVariables(const std::map<std::wstring, std::wstring>&, std::vector<std::wstring>* diagnostics);`
  - TAP: `void MergeResourceVariablesForThisThread(const styler::ResolvedTheme&)`, `void UnmergeResourceVariablesForThisThread()`.

- [ ] **Step 1: Teste e parser das chaves (core)**

`tests/core/test_resource_variables.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/resource_variables.h>

using namespace styler;

TEST_CASE("plain key is an untyped string override") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"TaskbarHeight", L"40"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].key == L"TaskbarHeight");
    CHECK(v[0].value == L"40");
    CHECK(v[0].theme == ResourceTheme::None);
    CHECK(v[0].type == ResourceValueType::String);
}

TEST_CASE("@Dark and @Light select the theme dictionary") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables(
        {{L"AdaptiveFill@Light", L"#FFFFFF"}, {L"AdaptiveFill@Dark", L"#000000"}}, &diag);
    REQUIRE(v.size() == 2);
    CHECK(v[0].key == L"AdaptiveFill");
    CHECK(v[0].theme == ResourceTheme::Dark);  // map order: "@Dark" < "@Light"
    CHECK(v[1].theme == ResourceTheme::Light);
}

TEST_CASE("a trailing colon marks a XAML value") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"Brush:", L"<SolidColorBrush Color=\"Red\"/>"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].key == L"Brush");
    CHECK(v[0].type == ResourceValueType::Xaml);
}

TEST_CASE("a ThemeResource value becomes a reference to that key") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables(
        {{L"Accent1@Dark", L"{ThemeResource SystemAccentColorLight3}"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == ResourceValueType::ThemeResourceReference);
    CHECK(v[0].value == L"SystemAccentColorLight3");
}

TEST_CASE("an unknown theme suffix is dropped with a diagnostic") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"X@Blue", L"1"}}, &diag);
    CHECK(v.empty());
    CHECK(diag.size() == 1);
}
```

`src/core/include/styler/resource_variables.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

namespace styler {

enum class ResourceTheme { None, Dark, Light };
enum class ResourceValueType { String, Xaml, ThemeResourceReference };

// One `resourceVariables` entry, key syntax decoded: `Name` overrides an
// existing application resource (converted to its existing type at merge
// time); `Name@Dark` / `Name@Light` adds to a theme dictionary; a trailing
// `:` on the name means the value is XAML; a `{ThemeResource Key}` value is
// a reference resolved at merge time (and refreshed when system colours
// change). Mirrors upstream ParseResourceVariable (vendor:19000-19080).
struct ResourceVariable {
    std::wstring key;
    std::wstring value;
    ResourceTheme theme = ResourceTheme::None;
    ResourceValueType type = ResourceValueType::String;
};

std::vector<ResourceVariable> ParseResourceVariables(
    const std::map<std::wstring, std::wstring>& variables,
    std::vector<std::wstring>* diagnostics);

}  // namespace styler
```

`src/core/resource_variables.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/resource_variables.h>

namespace styler {
namespace {

std::wstring Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    return std::wstring(s);
}

}  // namespace

std::vector<ResourceVariable> ParseResourceVariables(
    const std::map<std::wstring, std::wstring>& variables,
    std::vector<std::wstring>* diagnostics) {
    std::vector<ResourceVariable> out;
    for (const auto& [raw_key, raw_value] : variables) {
        ResourceVariable v;
        std::wstring key = Trim(raw_key);
        v.value = Trim(raw_value);

        if (!key.empty() && key.back() == L':') {
            v.type = ResourceValueType::Xaml;
            key = Trim(key.substr(0, key.size() - 1));
        } else if (v.value.starts_with(L"{ThemeResource ") && v.value.ends_with(L"}")) {
            v.type = ResourceValueType::ThemeResourceReference;
            v.value = Trim(std::wstring_view(v.value).substr(
                15, v.value.size() - 16));  // strlen("{ThemeResource ") == 15
        }

        if (auto at = key.find(L'@'); at != std::wstring::npos) {
            std::wstring theme = Trim(std::wstring_view(key).substr(at + 1));
            key = Trim(std::wstring_view(key).substr(0, at));
            if (theme == L"Dark") {
                v.theme = ResourceTheme::Dark;
            } else if (theme == L"Light") {
                v.theme = ResourceTheme::Light;
            } else {
                if (diagnostics) {
                    diagnostics->push_back(L"resource variable '" + raw_key +
                                           L"': unknown theme '" + theme +
                                           L"', expected Dark or Light");
                }
                continue;
            }
        }
        if (key.empty()) {
            if (diagnostics) {
                diagnostics->push_back(L"resource variable with empty name skipped");
            }
            continue;
        }
        v.key = std::move(key);
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace styler
```

- [ ] **Step 2: A mesclagem (TAP)**

`winrt_common.h` ganha `#include <winrt/Windows.UI.Xaml.Interop.h>` e `#include <winrt/Windows.UI.ViewManagement.h>`.

`src/tap/resource_variables.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <styler/matcher.h>

namespace styler::tap {

// Application::Current().Resources() is per UI thread, so merging is too.
// Idempotent per theme: a second call with the same theme is a no-op; a
// different theme unmerges the previous one first. Called by the engine on
// the first element it styles on a thread (merging earlier, at window
// creation, does not stick - upstream's observation, vendor:18181).
void MergeResourceVariablesForThisThread(const styler::ResolvedTheme& theme);

// Restores every overridden application resource to its saved original and
// removes the merged theme dictionary. Safe when nothing was merged.
void UnmergeResourceVariablesForThisThread();

}  // namespace styler::tap
```

`src/tap/resource_variables.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/resource_variables.h>

#include <unordered_map>
#include <vector>

#include <styler/resource_variables.h>
#include <tap/log.h>
#include <tap/property_setter.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

thread_local const styler::ResolvedTheme* t_merged_theme = nullptr;
thread_local wux::ResourceDictionary t_theme_dict{nullptr};
thread_local std::unordered_map<std::wstring, wf::IInspectable> t_originals;
thread_local std::vector<styler::ResourceVariable> t_entries;
thread_local winrt::Windows::UI::ViewManagement::UISettings t_ui_settings{nullptr};
thread_local winrt::event_token t_colors_token{};

// `<Setter Property="Tag"><Setter.Value>...` on FrameworkElement: the same
// XamlReader path styles use, aimed at a property every element has
// (upstream ParseXamlValue, vendor:19232-19245).
wf::IInspectable ParseXamlValue(const std::wstring& xaml) {
    styler::PreparedStyle probe;
    probe.property = L"Tag";
    probe.value = xaml;
    probe.is_xaml = true;
    return ResolveSetter(L"FrameworkElement", L"", probe).value;
}

wf::IInspectable ConvertToExistingType(wf::IInspectable const& existing,
                                       const std::wstring& text) {
    winrt::hstring cls = winrt::get_class_name(existing);
    // Unwrap IReference<T> so ConvertValue sees the inner type.
    constexpr std::wstring_view kRef = L"Windows.Foundation.IReference`1<";
    if (std::wstring_view(cls).starts_with(kRef) && cls.back() == L'>') {
        cls = winrt::hstring(std::wstring_view(cls).substr(
            kRef.size(), cls.size() - kRef.size() - 1));
    }
    return wux::Markup::XamlBindingHelper::ConvertValue(
        wux::Interop::TypeName{cls, wux::Interop::TypeKind::Metadata},
        winrt::box_value(winrt::hstring(text)));
}

wf::IInspectable ValueFor(wux::ResourceDictionary const& resources,
                          const styler::ResourceVariable& v,
                          wf::IInspectable const& existing) {
    switch (v.type) {
        case styler::ResourceValueType::Xaml:
            return v.value.empty() ? nullptr : ParseXamlValue(v.value);
        case styler::ResourceValueType::ThemeResourceReference:
            return resources.Lookup(winrt::box_value(winrt::hstring(v.value)));
        case styler::ResourceValueType::String:
        default:
            // Theme-dictionary entries are boxed strings (XAML converts at
            // use); plain overrides take the existing resource's type.
            return existing ? ConvertToExistingType(existing, v.value)
                            : winrt::box_value(winrt::hstring(v.value));
    }
}

void RefreshReferences() {
    try {
        auto resources = wux::Application::Current().Resources();
        wux::ResourceDictionary dark{nullptr}, light{nullptr};
        if (t_theme_dict) {
            dark = t_theme_dict.ThemeDictionaries()
                       .TryLookup(winrt::box_value(L"Dark")).try_as<wux::ResourceDictionary>();
            light = t_theme_dict.ThemeDictionaries()
                        .TryLookup(winrt::box_value(L"Light")).try_as<wux::ResourceDictionary>();
        }
        for (const auto& v : t_entries) {
            if (v.type != styler::ResourceValueType::ThemeResourceReference) {
                continue;
            }
            auto key = winrt::box_value(winrt::hstring(v.key));
            auto value = resources.Lookup(winrt::box_value(winrt::hstring(v.value)));
            if (v.theme == styler::ResourceTheme::Dark && dark) {
                dark.Insert(key, value);
            } else if (v.theme == styler::ResourceTheme::Light && light) {
                light.Insert(key, value);
            } else {
                resources.Insert(key, value);
            }
        }
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"refresh resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
}

}  // namespace

void MergeResourceVariablesForThisThread(const styler::ResolvedTheme& theme) {
    if (t_merged_theme == &theme) {
        return;
    }
    UnmergeResourceVariablesForThisThread();
    t_merged_theme = &theme;
    if (theme.resource_variables.empty()) {
        return;
    }
    std::vector<std::wstring> diag;
    t_entries = styler::ParseResourceVariables(theme.resource_variables, &diag);
    for (const auto& line : diag) {
        STYLER_LOG(LogLevel::Error, L"%s", line.c_str());
    }
    try {
        auto resources = wux::Application::Current().Resources();
        wux::ResourceDictionary dark, light;
        bool any_theme = false, any_reference = false;

        for (const auto& v : t_entries) {
            try {
                auto key = winrt::box_value(winrt::hstring(v.key));
                if (v.theme != styler::ResourceTheme::None) {
                    auto& target = v.theme == styler::ResourceTheme::Dark ? dark : light;
                    if (target.HasKey(key)) {
                        continue;
                    }
                    target.Insert(key, ValueFor(resources, v, nullptr));
                    any_theme = true;
                } else {
                    auto existing = resources.TryLookup(key);
                    if (!existing) {
                        STYLER_LOG(LogLevel::Error, L"resource '%s' not found, skipped",
                                   v.key.c_str());
                        continue;
                    }
                    if (!t_originals.try_emplace(v.key, existing).second) {
                        continue;  // Already overridden once; keep the first original.
                    }
                    resources.Insert(key, ValueFor(resources, v, existing));
                }
                any_reference |= v.type == styler::ResourceValueType::ThemeResourceReference;
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"resource '%s': 0x%08X", v.key.c_str(),
                           static_cast<unsigned>(ex.code()));
            }
        }
        if (any_theme) {
            t_theme_dict = wux::ResourceDictionary();
            t_theme_dict.ThemeDictionaries().Insert(winrt::box_value(L"Dark"), dark);
            t_theme_dict.ThemeDictionaries().Insert(winrt::box_value(L"Light"), light);
            resources.MergedDictionaries().Append(t_theme_dict);
        }
        if (any_reference) {
            t_ui_settings = winrt::Windows::UI::ViewManagement::UISettings();
            auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            t_colors_token = t_ui_settings.ColorValuesChanged(
                [queue](auto&&, auto&&) {
                    if (queue) {
                        queue.TryEnqueue([] { RefreshReferences(); });
                    }
                });
        }
        STYLER_LOG(LogLevel::Info, L"merged %zu resource variables on thread %lu",
                   t_entries.size(), GetCurrentThreadId());
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"merge resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"merge resources threw");
    }
}

void UnmergeResourceVariablesForThisThread() {
    if (!t_merged_theme) {
        return;
    }
    try {
        if (t_ui_settings) {
            t_ui_settings.ColorValuesChanged(t_colors_token);
            t_ui_settings = nullptr;
        }
        auto resources = wux::Application::Current().Resources();
        for (const auto& [key, original] : t_originals) {
            resources.Insert(winrt::box_value(winrt::hstring(key)), original);
        }
        if (t_theme_dict) {
            auto merged = resources.MergedDictionaries();
            uint32_t index = 0;
            if (merged.IndexOf(t_theme_dict, index)) {
                merged.RemoveAt(index);
            }
            t_theme_dict = nullptr;
        }
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"unmerge resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
    t_originals.clear();
    t_entries.clear();
    t_merged_theme = nullptr;
}

}  // namespace styler::tap
```

Em `style_engine.cpp`: no início de `OnElementAdded`, logo depois de obter `theme`, `MergeResourceVariablesForThisThread(*theme);`; em `RestoreAllOnThisThread`, antes do log final, `UnmergeResourceVariablesForThisThread();`.

CMake: `resource_variables.cpp` do core em `styler_core`; o do TAP no alvo MODULE; o teste em `tests/core`.

- [ ] **Step 3: Build e smoke**

`apply Pills` (usa `AdaptiveFill@Light/@Dark`): no log, `merged 6 resource variables on thread …`; a taskbar troca de visual. `reset`: os recursos voltam (não há como ver isso diretamente — o que se vê é a taskbar padrão sem resíduo de cor). Alterne o modo claro/escuro do Windows com o tema aplicado: nada crasha, e os elementos que usam `AdaptiveFill` acompanham. Anote.

- [ ] **Step 4: `docs/smoke-test.md` — a seção "Aplicar e desfazer"**

Acrescente ao arquivo existente:

```markdown
## Aplicar e desfazer (Plano 3)

Pré-requisito: `cmake --build build`; Explorer reiniciado se a DLL estava carregada.

1. `build\src\cli\taskbar-styler.exe list` — imprime 55 temas.
2. `apply TranslucentTaskbar` — carrega o TAP; a linha `Rectangle#BackgroundStroke`
   some e o fundo muda. Log: `theme TranslucentTaskbar: … prepared`,
   `initial apply: … 0 failed`.
3. `apply Lucent` (sem reiniciar o Explorer) — log `reload requested` …
   `reload applied`; visual troca.
4. Passe o mouse sobre um botão de app aberto: hover/press conforme o tema; ao
   sair, volta. Com `"logLevel":"debug"` no config, cada troca loga
   `apply <id> state '<Estado>'`.
5. `reset` — taskbar padrão; **`BackgroundStroke` reaparece** (prova de que o
   original foi restaurado).
6. `apply Pills` — log `merged 6 resource variables`; alterne claro/escuro no
   Windows: sem crash, cores acompanham.
7. Segundo monitor (se houver): a taskbar dele também estiliza; log mostra
   `initialized for thread` para a thread dela.
8. Abra e feche o menu Iniciar, a central de notificações e o flyout de
   configurações rápidas com um tema aplicado: sem crash; log mostra
   `drained N handles, M held` após cada rajada.
9. **Handles estáveis**: deixe a taskbar parada 10 minutos com tema aplicado.
   A última linha `drained … M held` não deve mudar de M, e `released so far`
   não deve crescer. Um M que cresce com a taskbar parada é vazamento — reporte
   com o tema e o log.

O que ainda é aproximação (Plano 3b): `WindhawkBlur` vira `AcrylicBrush`
(sem ruído e sem `BlurAmount`); capturas `=>` e valores `{{…}}` são pulados —
o log de `theme …` diz quantos.
```

- [ ] **Step 5: README e CI**

README: seção de uso com `load`, `apply <ThemeId>`, `reset`, `list`, `status`; onde fica o `config.json` (`%APPDATA%\TaskbarStyler`); o que é aproximado/pulado, com a mesma frase do smoke-test. Uma linha: "Trocar de tema não reinicia o Explorer; descarregar o TAP, sim (spec §6.4)."

`.github/workflows/ci.yml`: no passo "Artefatos do TAP e do CLI existem", somar `test -f build/src/cli/themes/TranslucentTaskbar.json` (a cópia da Task 1 aconteceu). O grep anti-injeção **não** ganha `RtlQueryFeatureConfiguration`: é consulta, e a Global Constraint diz por quê.

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core src/tap docs/smoke-test.md README.md .github/workflows/ci.yml
git commit -m "feat: variaveis de recurso por thread; checklist de aplicar/desfazer; CI"
```

---

## Auto-revisão do plano

**Cobertura da spec.** §2 "aplicar qualquer um dos 54 temas" → Tasks 5–8 (com as aproximações declaradas); "trocar em tempo real" → Task 7; "desfazer, restaurando os originais" → Tasks 5–7 (`RestoreAllOnThisThread`, `reset`); §4.2 → Task 7 (`config.json` + Event, uma direção); §4.3 "lógica crescendo dentro do TAP é sinal de que deveria estar no core" → Tasks 2, 3 e o parser da 8 no core; §6.1 estado por thread → Tasks 4–6, 8; §6.4 "Desativar tema" = `reset` (a DLL fica residente e inerte — `StopSubscription` existe mas só `SetSite(nullptr)` a chama); §7.1 → cada callback novo (`StandingCallback`, `CurrentStateChanged`, `PropertyChanged`, o tick do timer) é catch-all e devolve `S_OK`; §7.2 dreno adiado → Task 4, medidor de handles presos (`held`) no log a cada dreno → Task 4/8 (a bandeja do Plano 4 lê de lá); §7.6 → regras mortas descartadas em `PrepareTheme`, falha por regra logada e contada, tema inválido = nenhum tema; §8.2 → Task 8; §11 critérios 1 (aproximado, Plano 3b fecha), 4 (`reset`), 5 (item 9 do smoke).

**Lacuna assumida:** o §7.2 pede o contador "exposto no menu da bandeja" — não há bandeja até o Plano 4; o número existe no log desde a Task 4.

**Consistência de tipos entre tasks (conferida):** `PreparedStyle{property, visual_state, value, is_xaml}` (T3) é o que `ResolveSetter` (T5) consome e o que `ParseXamlValue` (T8) reaproveita; `RuleMatch::vsg` é `std::optional<VisualStateGroupRef{name, ancestor_depth}>` (T3) e é assim que T6 o lê; `ElementId` (T4) é o que `OnElementAdded/Removed/ElementHasState` (T4 stub, T5 corpo) e o registro usam; `LoadTap(pid, tap, init_data)` (T1) é o que `CmdLoad`/`CmdApply` (T7) chamam; `InitializationData()` (T1) é o que `LoadConfiguredTheme` (T5/T7) lê; `ConfigPath()` sai de `theme_session` (T5) para `ipc.h` (T7) — quem executar a T7 remove a definição da T5.

**Ordem de execução:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8. As Tasks 2 e 3 são puras e podem rodar em paralelo com a 1 se o executor quiser; 4–7 são estritamente sequenciais (cada uma substitui corpos da anterior).

**Custo conhecido:** todo TU do TAP que inclui `winrt_common.h` paga ~20 s de compilação. São cinco (`tap_boundary`, `change_subscription`, `release_queue`, `style_engine`+`property_setter`, `resource_variables`, `element_registry`); um build limpo do TAP passa de segundos para ~2 minutos. Aceito; um PCH é otimização para depois.

