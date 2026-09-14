# Plano 3b — Fidelidade

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** os 55 temas renderizam como no mod: o `WindhawkBlur` vira o brush de composição real (blur gaussiano + tint + saturação + luminosidade + ruído), as 29 capturas `Prop=>Var` e os 168 valores `{{…}}` passam a ser aplicados em vez de pulados, e o fan-out entre threads deixa de poder travar o explorer.

**Architecture:** O `styler_core` ganha duas partes puras novas: um parser de `<WindhawkBlur …/>` para uma struct (`BlurSpec`) e o avaliador de expressões `{{…}}` do upstream (recursivo descendente, sem dependência externa). O TAP ganha os cinco efeitos D2D declarados à mão sobre o `IGraphicsEffectD2D1Interop` **do SDK**, o `XamlBlurBrush` (um `XamlCompositionBrushBaseT`), e o estado de variáveis de estilo por thread com escolha do capturador mais próximo. `thread_init.cpp` troca `SendMessageW` por `SendMessageTimeoutW`. A reciclagem do `ItemsRepeater` é a última task e começa por um spike: o defeito não é verificável estaticamente.

**Tech Stack:** C++20 (MSVC), C++/WinRT do Windows SDK 10.0.26100.0 (`Windows.UI.Xaml`, `Windows.UI.Composition`, `Windows.Graphics.Effects`), `<windows.graphics.effects.interop.h>` e `<d2d1effects.h>` do SDK, COM registration-free, CMake + Ninja, doctest, nlohmann/json.

**Spec:** `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md` — §5.3 (`<WindhawkBlur>` mantém o nome), §6.1 (múltiplas superfícies), §7.1 (regra zero), §7.2 (handles), §7.3 (log), §7.6 (falhar fechado), §8.2 (smoke), §11 (critério 1: resultado visualmente idêntico ao do mod).

**Herança do Plano 3:** `docs/superpowers/plano-3-decisoes.md` (a cauda lista o que cai aqui) e `docs/superpowers/plano-3-spike-assinatura-permanente.md` (por que visuais de composition nunca são resolvidos e por que o `Advise` roda numa thread nova).

## Global Constraints

As do Plano 3, verbatim:

- **Licença GPL-3.0.** Todo `.h`/`.cpp`/`.py` novo leva `// SPDX-License-Identifier: GPL-3.0-or-later`. Não em `CMakeLists.txt` nem em `*.yml`.
- **Idioma:** identificadores e comentários de código em **inglês**; documentação em **português**.
- **C++20**, MSVC do Visual Studio 2026 Community, Windows SDK 10.0.26100.0. **x64 apenas.**
- **Nenhuma API de injeção, nenhum patch de código.** Proibidos: `VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread`, `SetThreadContext`, MinHook, Detours. `GetProcAddress` em `ntdll.dll` para **consultar** `RtlQueryFeatureConfiguration` é leitura, não injeção — o grep do CI não o cobre e não deve passar a cobrir.
- **Zero acesso à rede em runtime.**
- **Nada de diálogo modal a partir do TAP.**
- **`styler_core` permanece livre de Windows.** Este plano o **modifica** (adições puras), mas nada nele inclui `<windows.h>` ou `winrt/`.
- **A fronteira COM mora em `tap_boundary.cpp`** (`DllGetClassObject`, `DllCanUnloadNow`, `SetSite`, `GetSite`); os callbacks de árvore moram junto do objeto que os implementa (spec §7.1). Toda entrada chamável pelo XAML é catch-all e devolve `S_OK`.
- **C++/WinRT só dos headers pré-gerados do SDK** (`Include\10.0.26100.0\cppwinrt\winrt\*.h`), linkando `WindowsApp.lib`, com `WINRT_LEAN_AND_MEAN`. **Sem NuGet, sem passo de `cppwinrt.exe` no build.**
- **Toda chamada WinRT dentro do TAP fica dentro de `try { } catch (winrt::hresult_error const&) { } catch (...) { }`.** Uma exceção C++/WinRT escapando de um callback do XAML é um crash dentro do explorer.
- **Estado por thread é `thread_local`.** Registro de elementos, estado de customização, fila de liberação e — novo neste plano — o estado de variáveis de estilo nunca cruzam threads (spec §6.1; upstream `vendor/upstream/windows-11-taskbar-styler.wh.cpp:11834`, `:18281`).
- **O callback da assinatura permanente nunca libera handle** (spec §7.2). Liberação só pela fila + dreno no dispatcher da thread.
- **Falhar fechado por regra, nunca pela metade por tema** (spec §7.6): uma regra que não aplica é logada em `Error` e as demais seguem.

Mais, específicas deste plano:

- **Sem projeção de `Microsoft.UI.Xaml`.** Ela não vem no SDK e `cppwinrt.exe` no build está proibido acima. Onde o upstream usa `muxc::ItemsRepeater` (Task 7), este plano usa **apenas tipos de `Windows.UI.Xaml`**: a reciclagem é detectada por um sinal do próprio elemento reciclado (`DataContextChanged`, ou a troca de `Visibility` que o pool de reciclagem faz), escolhido pelo spike do Step 0 daquela task. Se nenhum dos dois servir, a task é cortada com o motivo no ledger — nunca "resolvida" gerando projeção.
- **Sem NuGet.** Nenhum `packages.config`, nenhum `PackageReference`, nenhum `.winmd` lido em tempo de build.
- **Sem hooking.** Vale o mesmo do Plano 3: nem hook inline, nem `SetWindowsHookEx` além do `WH_CALLWNDPROC` já existente em `thread_init.cpp` (que é despacho de mensagem, não patch de código).
- **Nenhuma interface que o SDK já declara é redeclarada à mão.** `IGraphicsEffectD2D1Interop` vem de `<windows.graphics.effects.interop.h>`; os CLSIDs e enums vêm de `<d2d1effects.h>`. O upstream redeclara os dois porque um mod do Windhawk é um arquivo só — não é o nosso caso (ver Fatos).

## Fatos verificados antes de escrever este plano

Cada afirmação técnica abaixo foi conferida contra o SDK, o corpus ou o probe compilado — não contra memória. Quem executar não precisa re-derivar; quem duvidar tem onde olhar.

| Fato | Como foi verificado |
|---|---|
| O SDK 10.0.26100.0 traz `cppwinrt/winrt/Windows.UI.Composition.h`, `Windows.Graphics.Effects.h`, `Windows.UI.Xaml.Hosting.h`, `Windows.System.Power.h`, `Windows.Storage.Streams.h` e `Windows.UI.ViewManagement.h`. | `ls` no SDK, 2026-09-14. |
| `IGraphicsEffectD2D1Interop` **vem do SDK**, em `winrt/windows.graphics.effects.interop.h:53-83`, IID `2FC57384-A068-44D7-A331-30982FCF7177`, com os seis métodos `GetEffectId` / `GetNamedPropertyMapping` / `GetPropertyCount` / `GetProperty` / `GetSource` / `GetSourceCount` e o enum `GRAPHICS_EFFECT_PROPERTY_MAPPING`. O upstream a redeclara à mão em `vendor:12562-12643` porque um mod do Windhawk é um arquivo só. | `sed` no header do SDK, 2026-09-14. |
| **Probe compilado, 0 erros, 15 s, obj de 673 KB:** `wuxm::XamlCompositionBrushBaseT<T>` com `OnConnected` / `OnDisconnected` / `CompositionBrush()`; `winrt::implements<T, wge::IGraphicsEffect, wge::IGraphicsEffectSource, awge::IGraphicsEffectD2D1Interop>`; `ElementCompositionPreview::GetElementVisual(e).Compositor()`; `Compositor::CreateBackdropBrush` / `CreateEffectFactory` / `CreateColorBrush` / `CreateSurfaceBrush`; `CompositionEffectFactory::CreateBrush` + `SetSourceParameter`; `LoadedImageSurface::StartLoadFromStream`; `CompositionStretch::None`; `UISettings::AdvancedEffectsEnabled(+Changed)`; `PowerManager::EnergySaverStatusChanged`; `DependencyObject::RegisterPropertyChangedCallback` / `UnregisterPropertyChangedCallback`; `FrameworkElement::SizeChanged(winrt::auto_revoke, …)`. Flags: `/std:c++20 /EHsc /permissive- /W4 /utf-8 /DWINRT_LEAN_AND_MEAN /bigobj` sob o VsDevCmd. | Probe `blur_probe.cpp` no scratchpad, 2026-09-14. |
| Os métodos das interfaces ABI **não levam `override` nem `IFACEMETHODIMP`**: `winrt::implements` despacha por CRTP (`produce<D,I>` chama `shim().Método(...)`), então `override` dá **C3668** ("não substituiu nenhum método de classe base"). Assinatura correta: `HRESULT Nome(args…) noexcept`. | Erro observado e corrigido no mesmo probe. |
| Usando o header do SDK, `GetProperty` e `GetSource` **têm de usar os tipos midl** `ABI::Windows::Foundation::IPropertyValue**` e `ABI::Windows::Graphics::Effects::IGraphicsEffectSource**`. Com `winrt::impl::abi_t<…>**` (a forma do upstream, que casa com a declaração *dele*) o compilador dá **C2259**: a classe abstrata do SDK continua sem implementação. A ponte é um `reinterpret_cast` — os dois tipos são binariamente idênticos. | Erro observado e corrigido no mesmo probe. |
| A especialização `template <> inline constexpr winrt::guid winrt::impl::guid_v<winrt::impl::abi_t<wf::IPropertyValue>>{…}` continua **obrigatória**, senão `.as<abi_t<IPropertyValue>>()` não compila. | `vendor:12490-12493`; probe. |
| `CLSID_D2D1GaussianBlur`, `CLSID_D2D1ColorMatrix`, `CLSID_D2D1Composite`, `CLSID_D2D1Flood` e `CLSID_D2D1Border` estão em `um/d2d1effects.h:30-43`; os enums `D2D1_GAUSSIANBLUR_PROP_*`, `D2D1_COLORMATRIX_PROP_*`, `D2D1_FLOOD_PROP_*`, `D2D1_BORDER_PROP_*`, `D2D1_COMPOSITE_MODE` e `D2D1_BORDER_EDGE_MODE_WRAP` vêm do mesmo header. | `grep` no SDK, 2026-09-14. |
| Grafo de efeitos do upstream, em ordem: `GaussianBlur(source="backdrop")` → (se `TintSaturation` ≠ 1) `ColorMatrix` de saturação → (se `TintLuminosityOpacity` > 0) `ColorMatrix` de luminosidade → (se `NoiseOpacity` > 0) `Composite(topo, ColorMatrix(Border(noise)))` → `Composite(topo, Flood(tint))`. Depois `factory.CreateBrush()` e `SetSourceParameter(L"backdrop", compositor.CreateBackdropBrush())`. Coeficientes de luma Rec. 709 (0.2126 / 0.7152 / 0.0722). | `vendor:13682-13800`. |
| O ruído é um BMP 256×256 gerado proceduralmente (`std::mt19937` semeado com 0, LUT de `pow(i/255, 1/density)`), servido por `InMemoryRandomAccessStream` e cacheado por densidade em `thread_local`. | `vendor:12412-12470`. |
| Tint por `{ThemeResource Key}`: o upstream cria `<SolidColorBrush Color="{ThemeResource Key}"/>` por `XamlReader::Load`, insere-o em `element.Resources()` com uma chave única (`__WhBlurProxy_N`), lê `proxy.Color()` e observa `RegisterPropertyChangedCallback(SolidColorBrush::ColorProperty())` para refazer o brush quando o tema claro/escuro muda. O destrutor remove a chave. | `vendor:13382-13470`, `:13645-13665`, `:13818-13830`. |
| `ShouldUseFallback()` do upstream **retorna `false` de saída** quando não há `FallbackColor` nem `FallbackColor="{ThemeResource …}"`; só então consulta `HKLM\SYSTEM\CurrentControlSet\Control\Power\EnergySaverState` e `UISettings::AdvancedEffectsEnabled`. | `vendor:13831-13842`. |
| **Corpus, blur:** 272 tags `<WindhawkBlur>` (231 inline em estilos, 41 dentro de constantes, **0** em `resourceVariables`), 69 textos distintos, 32 temas. Atributos: `BlurAmount` 272 (271 numéricos + 1 `$taskbarBlurIncreace`), `TintColor` 267 (233 `#hex`, 33 `{ThemeResource …}`, 1 `$aeroColor`), `TintOpacity` 40, `TintSaturation` 10, `TintLuminosityOpacity` 6, `NoiseOpacity` 5, `NoiseDensity` 5. **`FallbackColor`: zero ocorrências.** Ruído em 4 temas (`LayerMicaUI` e os três `Luminosity_*`). | `python` sobre `themes/*.json`, 2026-09-14. |
| **Corpus, variáveis:** 29 capturas `Prop=>Var` e **168** referências `{{…}}` (o "108" do Plano 3 contava estilos, não referências), nos **mesmos 14 temas**. 48 referências são a forma pura `{{Var}}`; 120 aparecem misturadas com texto (`Padding := {{a}},{{b}},{{c}},{{d}}`). Zero `{{…}}` em `target`. Duas em constantes, ambas de `Pills` (`{{__unset}}`). Propriedades capturadas: `ActualWidth` 21, `ActualHeight` 6, `Height` 2. **Nenhum tema captura o mesmo nome duas vezes.** | `python` sobre `themes/*.json`, 2026-09-14. |
| Operadores realmente usados dentro de `{{…}}` no corpus: `-` 61, `*` 28, `max(` 22, `+` 20, `min(` 19, `/` 18, `?:` 12, `>` 12, literal de crase 8. **Não usados:** `==`, `!=`, `<`, `<=`, `>=`. | mesma medição. |
| A gramática completa de `{{…}}` está especificada no readme do upstream: números, literais de crase (crase dobrada escapa), referências a variável, `+ - * /` e unários, `< <= == >= > !=` (valem 1/0), `cond ? a : b`, `min(a,b)`, `max(a,b)`, parênteses. Pares de chave casam **do mais interno para fora** (`{{{x}}}` = `{` + substituição + `}`). Variável indefinida dentro de expressão vale string vazia; `{{Var}}` puro de variável indefinida **pula o estilo**. | `vendor:375-410`. |
| O avaliador do upstream é um recursivo-descendente de ~440 linhas (`StyleExpressionValue` + `StyleVariableExpressionEvaluator`, `vendor:16208-16650`), seguido de `EvaluateStyleVariableExpression` (`:16658`) e `ExpandStyleVariables` (`:16707`, que varre `}}` da esquerda e casa o `{{` mais à direita antes dele). Tudo texto puro — cabe no `styler_core`. | leitura do vendor, 2026-09-14. |
| Captura: o valor é lido com `DependencyObject::GetValue` (**não** `ReadLocalValue`), porque `ActualWidth` nunca tem valor local. Propriedades de layout (`ActualWidth`/`ActualHeight`) **não disparam** `RegisterPropertyChangedCallback`; o upstream assina `FrameworkElement::SizeChanged` para elas e o callback de propriedade só para as demais. | `vendor:16768-16790`, `:17199-17262`. |
| Escolha do capturador mais próximo = maior profundidade do ancestral comum (`ElementTreeLcaDepth`), empate resolvido pelo capturador mais recente. | `vendor:11728-11750`, `vendor:363-367`. |
| `HandleVirtualizingRepeater` assina `ElementClearing`/`ElementPrepared` em `muxc::ItemsRepeater` e, em cada um, refaz a customização da **subárvore inteira** do item (`ReapplyCustomizationsForSubtree`), porque o pool de reciclagem **recolhe o elemento e o mantém parenteado** — logo a diagnostics não reporta Remove/Add. | `vendor:18114-18175`. |
| 18 temas citam `ItemsRepeater` em seletores (88 ocorrências), incluindo `Microsoft.UI.Xaml.Controls.ItemsRepeater#TaskbarFrameRepeater`. | `grep` no corpus, 2026-09-14. |
| `Microsoft.UI.Xaml.winmd` existe na máquina (`C:\Windows\SystemApps\Microsoft.UI.Xaml.CBS_8wekyb3d8bbwe\`), mas usá-lo exigiria `cppwinrt.exe` no build ou uma declaração ABI à mão de `IItemsRepeater` inteira (vtable com dezenas de slots antes dos dois eventos). Proibido pelas Global Constraints e desproporcional. | `find`, 2026-09-14. |
| `VisualElement::NumChildren` existe em `xamlom.h:176` e hoje não é lido por `tree_export.cpp` — um lote parcial sai como árvore truncada plausível com `S_OK`. | `grep` no SDK e em `src/tap/tree_export.cpp`, 2026-09-14. |
| `RunOnWindowThread` usa `SendMessageW` sem timeout (`src/tap/thread_init.cpp`, dentro de `RunOnWindowThread`), numa thread de UI que pode estar travada. | leitura do arquivo, 2026-09-14. |
| Hoje o TAP desenha o blur como `AcrylicBrush`: `RewriteWindhawkBlur` (`src/core/blur_rewrite.cpp`) mantém `TintColor` / `TintOpacity` / `TintLuminosityOpacity` / `FallbackColor` e descarta `BlurAmount` / `TintSaturation` / `NoiseOpacity` / `NoiseDensity`. `PrepareTheme` aplica isso tanto nas constantes resolvidas quanto nos estilos `is_xaml` (`src/core/matcher.cpp:163-168`, `:224-228`). | leitura dos arquivos, 2026-09-14. |
| O motor cacheia o `ResolvedSetter` por `PreparedStyle*` **por thread** e compartilha o mesmo objeto de valor entre todos os elementos que a regra casa (`src/tap/style_engine.cpp:215-230`). Um `XamlBlurBrush` **não pode** entrar nesse cache: ele guarda o `Compositor` do elemento e insere chaves no `Resources()` dele. | leitura do arquivo, 2026-09-14. |

## Fora deste plano

- **Troca automática para o brush de fallback** quando a economia de energia liga ou "Efeitos de transparência" é desligado (`vendor:13831-13905`, incluindo o watch de `RegNotifyChangeKeyValue` sobre a chave `Power`). Motivo medido: `ShouldUseFallback()` do upstream retorna `false` de saída sem `FallbackColor`, e **nenhuma das 272 tags do corpus tem `FallbackColor`** — o caminho inteiro é inalcançável para os 55 temas. O atributo continua sendo **parseado** (Task 2) e usado como cor do brush quando a criação do efeito falha; só o chaveamento dinâmico por energia/transparência fica de fora.
- **Escopo por `XamlRoot` do estado de variáveis** (`vendor:12054-12100`). Aqui o estado é `thread_local`, e cada superfície XAML da taskbar já roda na própria thread (spec §6.1). Decisão e custo registrados na Task 6.
- **Retry de imagem remota** (`TrackIfRemoteImageSource`) e **click-through** — fora do escopo do produto (spec §2). A referência `{{clickThroughTaskbar}}` que existe em um estilo do corpus resolve como variável indefinida e pula aquele estilo, que é o comportamento correto aqui.
- **Contador de handles VIVOS da spec §7.2** e o **deadlock de `SetSite(nullptr)`** (`UnregisterWaitEx(INVALID_HANDLE_VALUE)` + ramo `!site` ignorando `Deferred`) — herdados pelo **Plano 4** segundo a revisão final do Plano 3.

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `src/tap/thread_init.cpp` | `SendMessageTimeoutW` com `SMTO_ABORTIFHUNG` (Task 1) |
| `src/tap/tree_export.h/.cpp`, `src/tap/tree_format.cpp` | `Reported::num_children` + aviso de árvore incompleta (Task 1) |
| `src/core/include/styler/blur.h`, `src/core/blur.cpp` | `BlurSpec`, `ParseWindhawkBlur` — puro (Task 2) |
| `src/core/include/styler/blur_rewrite.h`, `src/core/blur_rewrite.cpp` | mantido: agora é só o fallback documentado (Task 2) |
| `src/core/include/styler/matcher.h`, `src/core/matcher.cpp` | `PreparedStyle::blur`, contadores (Tasks 2, 5) |
| `src/tap/blur_effects.h/.cpp` | Os cinco efeitos D2D + `CreateNoiseStream` (Task 3) |
| `src/tap/blur_brush.h/.cpp` | `XamlBlurBrush` (Task 4) |
| `src/tap/property_setter.h/.cpp` | `ResolvedSetter::blur`, criação por elemento (Task 4) |
| `src/tap/style_engine.cpp` | Não cachear brushes de blur; fechar o brush no restore (Task 4); capturas e consumidores (Task 6); reciclagem (Task 7) |
| `src/core/include/styler/style_expression.h`, `src/core/style_expression.cpp` | Avaliador `{{…}}` + `ExpandStyleVariables` — puro (Task 5) |
| `src/tap/style_variables.h/.cpp` | Estado por thread, capturas, consumidores, propagação (Task 6) |
| `tests/core/test_blur.cpp`, `tests/core/test_style_expression.cpp` | Testes das partes puras (Tasks 2, 5) |
| `docs/smoke-test.md`, `README.md` | Itens de fidelidade (Tasks 4, 6, 7) |

**Onde mora cada estado.** Nada muda no que o Plano 3 fixou: o `ResolvedTheme` é imutável e global; tudo que aponta para elemento XAML é `thread_local`. O estado novo deste plano (`t_style_variables` da Task 6, o cache de ruído da Task 3) segue a mesma regra. O `XamlBlurBrush` é o único objeto novo que referencia um elemento — e por `winrt::weak_ref`, só para poder remover as chaves de proxy que inseriu.

---

### Task 1: Fan-out com timeout e a árvore que não mente

Duas dívidas herdadas, ambas de uma linha de risco e nenhuma de escopo: `RunOnWindowThread` bloqueia para sempre numa thread de UI travada, e o export da árvore não distingue "a taskbar tem 4 filhos aqui" de "o lote acabou antes de os outros chegarem". Primeira task porque não depende de nada e porque toda task seguinte usa `RunOnWindowThread`.

**Files:**
- Modify: `src/tap/thread_init.cpp`, `src/tap/tree_export.h`, `src/tap/tree_export.cpp`, `src/tap/tree_format.cpp`, `src/tap/release_queue.cpp`, `src/tap/tap_boundary.cpp`
- Test: `tests/tap/test_tree_format.cpp`

**Interfaces:**
- Consumes: `ThreadProc`, `RunOnWindowThread` (Plano 3), `Reported`, `BuildForest` (Plano 2).
- Produces:
  - `Reported::num_children` (`unsigned int`) — o `VisualElement::NumChildren` do report.
  - `std::wstring styler::tap::DescribeIncompleteTree(const std::vector<Reported>&)` — vazio quando cada elemento reportado recebeu tantos filhos quanto declarou; caso contrário, uma linha por discrepância. Puro, testável.
  - `RunOnWindowThread` continua com a mesma assinatura e semântica de retorno (`false` = não despachou).

- [ ] **Step 1: `SendMessageTimeoutW`**

Em `src/tap/thread_init.cpp`, dentro de `RunOnWindowThread`, trocar a chamada e o `return`:

```cpp
    if (!hook) {
        return false;
    }

    // SendMessageW without a timeout parks this thread forever when the
    // target UI thread is wedged - and the caller here is often the reload
    // pool thread or SetSite, neither of which may hang the shell. The
    // timeout is generous (a XAML host busy with a layout pass legitimately
    // takes a while) but finite, and SMTO_ABORTIFHUNG returns immediately
    // when the window is already marked not-responding instead of waiting
    // out the full budget. Inherited from the Plano 2 ledger
    // ("SendMessageW sem timeout no thread_init.cpp") and repeated by the
    // Plano 3 final review.
    constexpr UINT kRunTimeoutMs = 5000;
    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(hWnd, RunMessage(), 0,
                                       reinterpret_cast<LPARAM>(&rp),
                                       SMTO_ABORTIFHUNG, kRunTimeoutMs,
                                       &result);
    UnhookWindowsHookEx(hook);

    if (!sent) {
        // `rp` lives on this stack frame and the hook is gone, so nothing can
        // reach it any more - but the proc may or may not have run. Callers
        // treat false as "not dispatched" and log; none of them retries in a
        // loop (spec section 6.5).
        STYLER_LOG(LogLevel::Error,
                   L"RunOnWindowThread timed out or failed for hwnd %p "
                   L"(thread %lu, error %lu)",
                   hWnd, thread_id, GetLastError());
        return false;
    }
    return true;
```

Remover as duas linhas antigas (`SendMessageW(...)` e o `UnhookWindowsHookEx(hook); return true;`).

> **Atenção ao tempo de vida:** `rp` está na pilha desta função. Com `SendMessageTimeoutW` e `SMTO_ABORTIFHUNG`, um timeout devolve o controle **enquanto a mensagem pode ainda estar enfileirada**. `UnhookWindowsHookEx` acontece antes do `return`, e é o hook que entrega o ponteiro: depois de removê-lo, nenhum `WH_CALLWNDPROC` novo o vê. Uma janela de corrida teórica sobra (um `CallWndProc` já em execução na outra thread), e é aceita: a alternativa seria alocar `RunParam` no heap e nunca poder liberá-lo. Documentar exatamente isso no comentário acima já feito.

- [ ] **Step 2: `NumChildren` chega ao `Reported`**

`src/tap/tree_export.h` — acrescentar o campo e documentar:

```cpp
struct Reported {
    unsigned long long handle = 0;
    unsigned long long parent = 0;
    unsigned int child_index = 0;
    // VisualElement::NumChildren as diagnostics declared it (xamlom.h:176).
    // The forest is built from the parent/child pairs alone; this is only
    // used to tell a legitimately small subtree from a batch that ended
    // early, which otherwise exports as a plausible but truncated tree with
    // S_OK (Plano 2 ledger).
    unsigned int num_children = 0;
    std::wstring type;
    std::wstring name;
};
```

e, junto das outras funções puras:

```cpp
// One line per element that was reported with more children than the stream
// actually delivered, in the order they were reported. Empty when the tree is
// complete. Pure: no XAML, no COM.
std::vector<std::wstring> DescribeIncompleteTree(
    const std::vector<Reported>& reported);
```

- [ ] **Step 3: preencher o campo**

`src/tap/tree_export.cpp`, em `OnVisualTreeChange`, logo depois de `r.child_index = relation.ChildIndex;`:

```cpp
                r.num_children = element.NumChildren;
```

- [ ] **Step 4: a função pura**

`src/tap/tree_format.cpp`, antes de `FormatTree`:

```cpp
std::vector<std::wstring> DescribeIncompleteTree(
    const std::vector<Reported>& reported) {
    // Count how many children each reported handle actually received. A
    // handle reported twice keeps its first report (same rule BuildForest
    // uses), so the declared count comes from the first entry too.
    std::map<unsigned long long, unsigned int> declared;
    std::map<unsigned long long, unsigned int> delivered;
    std::vector<unsigned long long> order;
    for (const auto& r : reported) {
        if (declared.emplace(r.handle, r.num_children).second) {
            order.push_back(r.handle);
        }
        if (r.parent != 0) {
            ++delivered[r.parent];
        }
    }

    std::vector<std::wstring> out;
    for (unsigned long long handle : order) {
        unsigned int want = declared[handle];
        auto it = delivered.find(handle);
        unsigned int got = it == delivered.end() ? 0u : it->second;
        if (got >= want) {
            continue;
        }
        // Find the element's own report for a readable name.
        std::wstring label;
        for (const auto& r : reported) {
            if (r.handle == handle) {
                label = r.type;
                if (!r.name.empty()) {
                    label += L'#';
                    label += r.name;
                }
                break;
            }
        }
        wchar_t buf[64]{};
        swprintf_s(buf, L": %u of %u children reported", got, want);
        out.push_back(label + buf);
    }
    return out;
}
```

Acrescentar `#include <map>` se ainda não estiver (já está: o arquivo inclui `<map>`).

- [ ] **Step 5: o export avisa**

`src/tap/tree_export.cpp`, em `ExportTreeToFile`, depois de montar o forest e antes de escrever o arquivo:

```cpp
    for (const auto& line : DescribeIncompleteTree(snapshot)) {
        STYLER_LOG(LogLevel::Error, L"incomplete visual tree: %s",
                   line.c_str());
    }
```

O export continua devolvendo `S_OK` e escrevendo o arquivo: uma árvore parcial ainda é útil para o usuário corrigir um seletor (spec §7.5). O que muda é que ela deixa de ser silenciosa.

- [ ] **Step 6: os dois nits de comentário herdados**

Em `src/tap/release_queue.cpp`, o comentário de `t_initial_apply_logged` (linhas 25-27) e em `src/tap/tap_boundary.cpp` (linha ~200) ainda chamam de "initial apply" o que o Plano 3 mediu ser **o primeiro dreno**, não o `SetSite`. Reescrever os dois para dizer "first drain on this thread", sem mudar código. (Herdados: "Dois nits de comentário pré-existentes … deferidos", `plano-3-decisoes.md`.)

- [ ] **Step 7: teste**

Acrescentar a `tests/tap/test_tree_format.cpp`:

```cpp
TEST_CASE("DescribeIncompleteTree flags a truncated batch") {
    using styler::tap::Reported;
    std::vector<Reported> reported{
        {1, 0, 0, 3, L"Taskbar.TaskbarFrame", L""},
        {2, 1, 0, 0, L"Grid", L"RootGrid"},
    };
    auto lines = styler::tap::DescribeIncompleteTree(reported);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == L"Taskbar.TaskbarFrame: 1 of 3 children reported");
}

TEST_CASE("DescribeIncompleteTree is quiet on a complete tree") {
    using styler::tap::Reported;
    std::vector<Reported> reported{
        {1, 0, 0, 2, L"Taskbar.TaskbarFrame", L""},
        {2, 1, 0, 0, L"Grid", L"RootGrid"},
        {3, 1, 1, 0, L"Rectangle", L"BackgroundFill"},
    };
    CHECK(styler::tap::DescribeIncompleteTree(reported).empty());
}
```

Os `Reported` já existentes nesse arquivo ganham um `0` a mais na inicialização agregada (o campo novo fica antes de `type`); ajustar todos.

**Test cycle:**

```
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build --output-on-failure
```

Esperado: `core` e `tap` verdes, com **2 casos a mais** em `tap`.

Smoke ao vivo (não requer tema): `build\src\cli\taskbar-styler.exe load` e depois `export`. No log:
- nenhuma linha `RunOnWindowThread timed out`;
- se aparecer `incomplete visual tree: …`, **isso é um achado a registrar no ledger**, não um erro da task — significa que o lote do Plano 2 sempre foi parcial e ninguém sabia.

**Commit:** `fix(tap): fan-out com timeout e aviso de arvore incompleta`

---

### Task 2: `BlurSpec` no core — o `<WindhawkBlur>` deixa de ser texto

Antes de qualquer linha de composition, o parser. Ele é puro, tem 69 entradas reais de corpus para testar e é o contrato que as Tasks 3 e 4 consomem. O `RewriteWindhawkBlur` **não é apagado**: vira o caminho de fallback, e o plano diz exatamente quando ele roda.

**Files:**
- Create: `src/core/include/styler/blur.h`, `src/core/blur.cpp`, `tests/core/test_blur.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `src/core/include/styler/blur_rewrite.h`, `src/core/include/styler/matcher.h`, `src/core/matcher.cpp`, `src/tap/theme_session.cpp`

**Interfaces:**
- Consumes: `ApplyStyleConstants`, `ResolveConstants` (Plano 3), `RewriteWindhawkBlur` (Plano 3).
- Produces:
  - `struct styler::BlurSpec` — os nove campos do `XamlBlurBrushParams` upstream.
  - `std::optional<BlurSpec> styler::ParseWindhawkBlur(std::wstring_view value)` — `nullopt` quando o texto não começa com a tag; **lança `styler::ParseError`** quando começa com a tag e está malformado (fechar fechado, spec §7.6).
  - `PreparedStyle::blur` (`std::optional<BlurSpec>`) — quando presente, `value` fica com a forma `AcrylicBrush` de fallback e `is_xaml` continua `true`.
  - `ResolvedTheme::blur_specs` (`int`) substitui a leitura de `blur_approximations` como "quantos blurs o tema tem"; `blur_approximations` passa a contar só os que **não** viraram `BlurSpec`.

- [ ] **Step 1: `src/core/include/styler/blur.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace styler {

// One `<WindhawkBlur .../>` pseudo-element, parsed. Field names and optional-
// ness mirror upstream's XamlBlurBrushParams (vendor:15373-15392) so the TAP
// side reads like the reference implementation.
//
// Colors are stored as straight ARGB bytes rather than a WinRT Color, since
// styler_core may not include winrt/. `tint_theme_resource` and
// `fallback_theme_resource` hold the KEY from `TintColor="{ThemeResource X}"`;
// when one is set the matching color field is a placeholder the TAP replaces
// with the live theme color.
struct BlurColor {
    std::uint8_t a = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const BlurColor&, const BlurColor&) = default;
};

struct BlurSpec {
    float blur_amount = 0.0f;
    BlurColor tint{};
    // Engaged when the markup carried TintOpacity: the value has already been
    // folded into `tint.a`, and the TAP re-applies it after resolving a theme
    // resource tint (whose own alpha must not win over the author's).
    std::optional<std::uint8_t> tint_opacity;
    std::wstring tint_theme_resource;
    std::optional<float> tint_luminosity_opacity;
    std::optional<float> tint_saturation;
    std::optional<float> noise_opacity;
    std::optional<float> noise_density;
    std::optional<BlurColor> fallback_color;
    std::wstring fallback_theme_resource;

    friend bool operator==(const BlurSpec&, const BlurSpec&) = default;
};

// Parses `<WindhawkBlur .../>` or its `<Blur .../>` synonym (spec section
// 5.3). Returns nullopt when `value` is not a blur element at all - that is
// the overwhelming majority of style values and is not an error. Throws
// styler::ParseError when the value IS a blur element but is malformed
// (unknown attribute, unterminated theme resource, unparseable number), so a
// typo fails the rule closed instead of silently rendering a different brush.
std::optional<BlurSpec> ParseWindhawkBlur(std::wstring_view value);

}  // namespace styler
```

- [ ] **Step 2: `src/core/blur.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/blur.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

#include <styler/selector.h>  // ParseError

namespace styler {
namespace {

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' ||
                          s.front() == L'\r' || s.front() == L'\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' ||
                          s.back() == L'\r' || s.back() == L'\n')) {
        s.remove_suffix(1);
    }
    return s;
}

[[noreturn]] void Fail(std::wstring_view what, std::wstring_view detail) {
    std::string msg = "WindhawkBlur: ";
    msg.append(what.begin(), what.end());
    msg += " '";
    msg.append(detail.begin(), detail.end());
    msg += "'";
    throw ParseError(msg);
}

float ParseFloat(std::wstring_view text) {
    std::wstring owned(text);
    wchar_t* end = nullptr;
    // wcstod, not stof: it reports where it stopped, so trailing junk
    // ("18px", "1,0") is rejected instead of silently truncated. The C
    // locale is the process default here and the corpus has no comma
    // decimals, so no locale juggling is needed.
    double v = std::wcstod(owned.c_str(), &end);
    if (end == owned.c_str() || *end != L'\0') {
        Fail(L"bad number", text);
    }
    return static_cast<float>(v);
}

int HexDigit(wchar_t c, std::wstring_view whole) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    Fail(L"bad hex color", whole);
}

std::uint8_t HexPair(std::wstring_view digits, size_t i,
                     std::wstring_view whole) {
    return static_cast<std::uint8_t>(HexDigit(digits[i], whole) * 16 +
                                     HexDigit(digits[i + 1], whole));
}

// #RGB, #ARGB, #RRGGBB, #AARRGGBB - the four forms XAML itself accepts. The
// 3- and 4-digit forms expand each nibble by duplication (0x8 -> 0x88), which
// is XAML's own rule.
BlurColor ParseColor(std::wstring_view text) {
    if (text.empty() || text.front() != L'#') {
        Fail(L"color must start with #", text);
    }
    std::wstring_view d = text.substr(1);
    auto nibble = [&](size_t k) -> std::uint8_t {
        return static_cast<std::uint8_t>(HexDigit(d[k], text) * 17);
    };
    BlurColor c{};
    switch (d.size()) {
        case 3:
            c.a = 0xFF;
            c.r = nibble(0);
            c.g = nibble(1);
            c.b = nibble(2);
            return c;
        case 4:
            c.a = nibble(0);
            c.r = nibble(1);
            c.g = nibble(2);
            c.b = nibble(3);
            return c;
        case 6:
            c.a = 0xFF;
            c.r = HexPair(d, 0, text);
            c.g = HexPair(d, 2, text);
            c.b = HexPair(d, 4, text);
            return c;
        case 8:
            c.a = HexPair(d, 0, text);
            c.r = HexPair(d, 2, text);
            c.g = HexPair(d, 4, text);
            c.b = HexPair(d, 6, text);
            return c;
        default:
            Fail(L"color must have 3, 4, 6 or 8 hex digits", text);
    }
}

// Splits `Name="value"` pairs. Quoted values may contain spaces, which is why
// this is a small scanner and not SplitStringView on ' ' - upstream's
// space-split needs a two-state machine to survive
// `TintColor="{ThemeResource X}"` (vendor:15200-15260); scanning to the
// closing quote has no such state.
struct Attribute {
    std::wstring_view name;
    std::wstring_view value;  // Without the quotes.
};

std::vector<Attribute> SplitAttributes(std::wstring_view body) {
    std::vector<Attribute> out;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == L' ' || body[i] == L'\t' ||
                                   body[i] == L'\r' || body[i] == L'\n')) {
            ++i;
        }
        if (i >= body.size()) {
            break;
        }
        size_t name_start = i;
        while (i < body.size() && body[i] != L'=' && body[i] != L' ') {
            ++i;
        }
        std::wstring_view name = body.substr(name_start, i - name_start);
        while (i < body.size() && body[i] == L' ') {
            ++i;
        }
        if (i >= body.size() || body[i] != L'=') {
            Fail(L"attribute without a value", name);
        }
        ++i;
        if (i >= body.size() || body[i] != L'"') {
            Fail(L"attribute value must be quoted", name);
        }
        ++i;
        size_t value_start = i;
        size_t close = body.find(L'"', i);
        if (close == std::wstring_view::npos) {
            Fail(L"unterminated attribute value", name);
        }
        out.push_back({name, body.substr(value_start, close - value_start)});
        i = close + 1;
    }
    return out;
}

// `{ThemeResource Key}` -> Key, or empty when the value is not one.
std::wstring_view ThemeResourceKey(std::wstring_view value) {
    constexpr std::wstring_view kPrefix = L"{ThemeResource";
    if (!value.starts_with(kPrefix) || !value.ends_with(L"}")) {
        return {};
    }
    std::wstring_view key =
        Trim(value.substr(kPrefix.size(), value.size() - kPrefix.size() - 1));
    if (key.empty()) {
        Fail(L"empty theme resource key", value);
    }
    return key;
}

}  // namespace

std::optional<BlurSpec> ParseWindhawkBlur(std::wstring_view value) {
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
        return std::nullopt;
    }
    if (!body.ends_with(L"/>")) {
        Fail(L"element must be self-closing", s);
    }
    body = body.substr(0, body.size() - 2);

    BlurSpec spec;
    std::optional<float> tint_opacity;
    for (const Attribute& attr : SplitAttributes(body)) {
        if (attr.name == L"BlurAmount") {
            spec.blur_amount = ParseFloat(attr.value);
        } else if (attr.name == L"TintColor") {
            if (auto key = ThemeResourceKey(attr.value); !key.empty()) {
                spec.tint_theme_resource = std::wstring(key);
            } else {
                spec.tint = ParseColor(attr.value);
            }
        } else if (attr.name == L"TintOpacity") {
            tint_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"TintLuminosityOpacity") {
            spec.tint_luminosity_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"TintSaturation") {
            spec.tint_saturation = ParseFloat(attr.value);
        } else if (attr.name == L"NoiseOpacity") {
            spec.noise_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"NoiseDensity") {
            spec.noise_density = ParseFloat(attr.value);
        } else if (attr.name == L"FallbackColor") {
            if (auto key = ThemeResourceKey(attr.value); !key.empty()) {
                spec.fallback_theme_resource = std::wstring(key);
            } else {
                spec.fallback_color = ParseColor(attr.value);
            }
        } else {
            Fail(L"unknown attribute", attr.name);
        }
    }

    // Same fold upstream does (vendor:15355-15365): TintOpacity overrides the
    // alpha the author may also have written into TintColor.
    if (tint_opacity) {
        float clamped = std::clamp(*tint_opacity, 0.0f, 1.0f);
        spec.tint.a = static_cast<std::uint8_t>(clamped * 255.0f);
        spec.tint_opacity = spec.tint.a;
    }
    return spec;
}

}  // namespace styler
```

> **Nota para quem implementar:** o bloco `case 3: case 4:` acima está escrito de forma redundante de propósito na primeira leitura — **limpe-o**: mantenha só a lambda `nibble` e as quatro linhas que a usam, e apague as lambdas mortas `nib`/`one` e a atribuição duplicada de `c.a`. Elas estão aqui apenas para que o revisor veja que a expansão de nibble é por duplicação (`0x8` → `0x88`), a regra do próprio XAML. O teste do Step 5 cobre `#RGB` e `#ARGB`.

- [ ] **Step 3: `blur_rewrite.h` vira fallback explícito**

Trocar o comentário do header (o código não muda):

```cpp
// The documented FALLBACK for `<WindhawkBlur .../>`. The primary path is
// ParseWindhawkBlur + the real composition brush (src/tap/blur_brush.h); this
// rewrite to a stock AcrylicBrush is what a style falls back to when the
// brush cannot be created - no Compositor for the element, an effect factory
// that refuses the graph, or a machine where Windows.UI.Composition is
// unavailable. It keeps the attributes the two brushes share by name -
// TintColor, TintOpacity, TintLuminosityOpacity, FallbackColor - and drops
// BlurAmount, TintSaturation, NoiseOpacity and NoiseDensity. `<Blur .../>` is
// accepted as a synonym (spec section 5.3). `*rewritten` reports whether
// anything changed, so the caller can count how many styles are running on
// the approximation. Any other value passes through.
```

- [ ] **Step 4: `PreparedStyle` carrega o spec**

`src/core/include/styler/matcher.h`:

```cpp
struct PreparedStyle {
    std::wstring property;
    std::wstring visual_state;  // Empty: unconditional.
    std::wstring value;
    bool is_xaml = false;
    // Engaged when `value` came from a `<WindhawkBlur .../>` element. `value`
    // then holds the AcrylicBrush fallback markup (blur_rewrite.h) and stays
    // usable as-is; the TAP prefers `blur` and only parses `value` when the
    // real brush cannot be built.
    std::optional<BlurSpec> blur;
};
```

e, em `ResolvedTheme`:

```cpp
    int blur_specs = 0;           // `<WindhawkBlur>` values parsed for the real brush.
    int blur_approximations = 0;  // Blur values that only got the AcrylicBrush rewrite.
```

Incluir `<styler/blur.h>` no topo de `matcher.h`.

- [ ] **Step 5: `PrepareTheme` parseia**

Em `src/core/matcher.cpp`, o laço das constantes (hoje linhas 163-168) **não muda**: uma constante continua sendo reescrita para `AcrylicBrush`, porque o valor dela é texto que será substituído dentro de um estilo e só lá se sabe se é um valor XAML. O que muda é o laço dos estilos: depois de `p.value = ApplyStyleConstants(...)` e do teste de `{{`, substituir o bloco do blur por:

```cpp
            if (p.is_xaml) {
                // The constants pass above already rewrote any blur that came
                // in through a $Constant, so parse the ORIGINAL text of this
                // style as well as the substituted one: a blur written inline
                // reaches here untouched, a blur that arrived via a constant
                // reaches here already as AcrylicBrush. Trying the raw value
                // first recovers the second case without undoing the rewrite
                // that resource variables still need.
                std::wstring raw = ApplyStyleConstants(v.value, raw_constants);
                std::optional<BlurSpec> spec = ParseWindhawkBlur(raw);
                if (spec) {
                    p.blur = std::move(spec);
                    ++out.blur_specs;
                    bool rewritten = false;
                    p.value = RewriteWindhawkBlur(raw, &rewritten);
                } else {
                    bool rewritten = false;
                    p.value = RewriteWindhawkBlur(p.value, &rewritten);
                    if (rewritten) {
                        ++out.blur_approximations;
                    }
                }
            }
```

onde `raw_constants` é uma segunda `ResolvedConstants` construída **antes** do laço de reescrita, a partir do mesmo `ResolveConstants(theme.constants)`:

```cpp
    ResolvedConstants constants = ResolveConstants(theme.constants);
    // A copy taken before the AcrylicBrush rewrite below, so the style loop
    // can see a blur constant in its original `<WindhawkBlur .../>` form and
    // parse it into a BlurSpec. `constants` itself keeps the rewritten form
    // because resource variables (merged into a XAML ResourceDictionary) have
    // no BlurSpec path and need real markup.
    const ResolvedConstants raw_constants = constants;
```

Um `ParseError` vindo de `ParseWindhawkBlur` **não** derruba o tema: envolver o bloco acima em `try`/`catch (const ParseError& ex)` que faz `out.diagnostics.push_back(theme.id + L": " + src.target + L": " + <what> + L" (blur)")`, mantém `p.blur` vazio e segue com o `RewriteWindhawkBlur` do ramo `else` — a mesma tolerância por regra da spec §7.6.

- [ ] **Step 6: o log conta os dois**

`src/tap/theme_session.cpp`, a linha de `Info`:

```cpp
        STYLER_LOG(LogLevel::Info,
                   L"theme %s: %zu rules prepared, %d captures and %d dynamic "
                   L"values skipped, %d blur brushes, %d blur approximations",
                   prepared->id.c_str(), prepared->rules.size(),
                   prepared->skipped_captures, prepared->skipped_dynamic,
                   prepared->blur_specs, prepared->blur_approximations);
```

- [ ] **Step 7: `tests/core/test_blur.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <styler/blur.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>

using styler::BlurSpec;
using styler::ParseWindhawkBlur;

TEST_CASE("ParseWindhawkBlur ignores anything that is not a blur element") {
    CHECK_FALSE(ParseWindhawkBlur(L"Transparent").has_value());
    CHECK_FALSE(ParseWindhawkBlur(L"<SolidColorBrush Color=\"#FF0000\"/>")
                    .has_value());
    CHECK_FALSE(ParseWindhawkBlur(L"").has_value());
}

TEST_CASE("ParseWindhawkBlur reads the shipped shape") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->blur_amount == doctest::Approx(18.0));
    CHECK(spec->tint.a == 0x25);
    CHECK(spec->tint.r == 0x32);
    CHECK(spec->tint.g == 0x32);
    CHECK(spec->tint.b == 0x32);
    CHECK_FALSE(spec->tint_opacity.has_value());
    CHECK(spec->tint_theme_resource.empty());
}

TEST_CASE("ParseWindhawkBlur folds TintOpacity into the tint alpha") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"10\" TintColor=\"#909090\" "
        L"TintOpacity=\"0.2\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->tint.a == 51);  // 0.2 * 255
    REQUIRE(spec->tint_opacity.has_value());
    CHECK(*spec->tint_opacity == 51);
}

TEST_CASE("ParseWindhawkBlur keeps a theme resource tint as a key") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"5\" "
        L"TintColor=\"{ThemeResource SystemChromeMediumColor}\" "
        L"TintOpacity=\"0.7\" />");
    REQUIRE(spec.has_value());
    CHECK(spec->tint_theme_resource == L"SystemChromeMediumColor");
    CHECK(spec->tint.a == 178);
}

TEST_CASE("ParseWindhawkBlur accepts the Blur synonym and short colors") {
    auto spec = ParseWindhawkBlur(L"<Blur BlurAmount=\"1\" TintColor=\"#8ABC\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->tint.a == 0x88);
    CHECK(spec->tint.r == 0xAA);
    CHECK(spec->tint.g == 0xBB);
    CHECK(spec->tint.b == 0xCC);
}

TEST_CASE("ParseWindhawkBlur reads noise, saturation and luminosity") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"30\" TintColor=\"#cc2a2e32\" "
        L"TintSaturation=\"1.8\" TintLuminosityOpacity=\"0.4\" "
        L"NoiseOpacity=\"0.03\" NoiseDensity=\"0.6\"/>");
    REQUIRE(spec.has_value());
    CHECK(*spec->tint_saturation == doctest::Approx(1.8));
    CHECK(*spec->tint_luminosity_opacity == doctest::Approx(0.4));
    CHECK(*spec->noise_opacity == doctest::Approx(0.03));
    CHECK(*spec->noise_density == doctest::Approx(0.6));
}

TEST_CASE("ParseWindhawkBlur fails closed on a malformed blur") {
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur Blur=\"1\"/>"),
                    styler::ParseError);
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur BlurAmount=\"18px\"/>"),
                    styler::ParseError);
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur BlurAmount=\"1\">"),
                    styler::ParseError);
    CHECK_THROWS_AS(
        ParseWindhawkBlur(L"<WindhawkBlur TintColor=\"{ThemeResource }\"/>"),
        styler::ParseError);
}

// The corpus is the real specification: every blur the 55 shipped themes
// contain must parse, and every one must reach a PreparedStyle with a spec.
TEST_CASE("every blur in the shipped corpus parses") {
    namespace fs = std::filesystem;
    int specs = 0;
    int themes_with_blur = 0;
    for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
        if (entry.path().extension() != ".json" ||
            entry.path().filename() == "credits.json") {
            continue;
        }
        styler::Theme theme = styler::LoadThemeFromFile(entry.path().wstring());
        styler::ResolvedTheme resolved = styler::PrepareTheme(theme);
        if (resolved.blur_specs > 0) {
            ++themes_with_blur;
        }
        specs += resolved.blur_specs;
        // Fail closed: a blur that only got the approximation means the
        // parser rejected markup the corpus actually ships.
        CHECK_MESSAGE(resolved.blur_approximations == 0,
                      "unparsed blur in ", entry.path().filename().string());
    }
    CHECK(themes_with_blur == 32);
    CHECK(specs == 272);
}
```

> `specs == 272` é o número medido de tags do corpus (231 inline + 41 vindas de constante). Se o número vier diferente, **isso é o achado**: ou o parser recusou algo, ou a contagem por estilo difere da contagem por tag porque um estilo referencia duas constantes de blur. Nesse caso, ajustar a asserção para o número medido **e registrar o motivo no ledger** — nunca abaixar a asserção sem explicação.

- [ ] **Step 8: CMake**

`src/core/CMakeLists.txt`: acrescentar `blur.cpp` à lista. `tests/core/CMakeLists.txt`: acrescentar `test_blur.cpp`.

**Test cycle:** build + `ctest`. Esperado: `core` verde com os 8 casos novos; a contagem de blurs impressa no log do TAP muda de `N blur approximations` para `272 blur brushes, 0 blur approximations` no agregado (por tema, o que aquele tema tiver).

Smoke: `apply TranslucentTaskbar` e conferir no log `theme TranslucentTaskbar: 8 rules prepared, … 2 blur brushes, 0 blur approximations`. A taskbar continua exatamente como no Plano 3 — nada consome `blur` ainda.

**Commit:** `feat(core): parser de WindhawkBlur para BlurSpec; AcrylicBrush vira fallback`

---

### Task 3: os cinco efeitos D2D no TAP

O grafo do `WindhawkBlur` é montado com efeitos que a projeção C++/WinRT não expõe: `Windows.UI.Composition` aceita qualquer objeto que implemente `IGraphicsEffect` **e** `IGraphicsEffectD2D1Interop`, e é assim que o Win2D (e o upstream) descrevem um efeito D2D sem D2D. Esta task entrega só os efeitos, compiláveis e testáveis isoladamente; o brush vem na Task 4.

**Files:**
- Create: `src/tap/blur_effects.h`, `src/tap/blur_effects.cpp`
- Modify: `src/tap/winrt_common.h`, `src/tap/CMakeLists.txt`

**Interfaces:**
- Consumes: `winrt_common.h` (Plano 3), `styler::BlurSpec` (Task 2).
- Produces, todos em `styler::tap`:
  - `struct GaussianBlurEffect` — `float BlurAmount`, `wge::IGraphicsEffectSource Source`.
  - `struct ColorMatrixEffect` — `std::array<float, 20> Matrix`, `Source`.
  - `struct CompositeEffect` — `D2D1_COMPOSITE_MODE Mode`, `std::vector<wge::IGraphicsEffectSource> Sources`.
  - `struct FloodEffect` — `winrt::Windows::UI::Color Color`.
  - `struct BorderEffect` — `D2D1_BORDER_EDGE_MODE ExtendX/ExtendY`, `Source`.
  - `wss::IRandomAccessStream CreateNoiseStream(float density)`.
  - `winrt::Windows::UI::Color ToWinRtColor(const styler::BlurColor&)`.

- [ ] **Step 1: `winrt_common.h` ganha os includes de composition**

Acrescentar, depois dos includes já existentes:

```cpp
// Composition + the D2D effect interop, for the WindhawkBlur brush
// (blur_effects.h, blur_brush.h). d2d1effects_2.h pulls d2d1effects_1.h and
// d2d1effects.h (the CLSIDs and the property enums) but NOT d2d1_1.h, which
// is where D2D1_COMPOSITE_MODE lives - measured: without the explicit
// include, blur_effects.h fails with C3646 on CompositeEffect::Mode.
// windows.graphics.effects.interop.h is the SDK's own declaration of
// IGraphicsEffectD2D1Interop - the upstream mod redeclares it by hand only
// because a Windhawk mod is a single file.
#include <d2d1_1.h>
#include <d2d1effects_2.h>
#include <windows.graphics.effects.interop.h>

#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

// Required for `.as<winrt::impl::abi_t<IPropertyValue>>()`, which is how an
// IGraphicsEffectD2D1Interop::GetProperty implementation hands a boxed value
// back across the ABI. Without it the cast does not compile
// (vendor:12490-12493, confirmed by the plan's probe).
template <>
inline constexpr winrt::guid winrt::impl::guid_v<
    winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>>{
    winrt::impl::guid_v<winrt::Windows::Foundation::IPropertyValue>};
```

e, junto dos outros aliases de namespace:

```cpp
namespace wge = winrt::Windows::Graphics::Effects;
namespace awge = ABI::Windows::Graphics::Effects;
namespace wuc = winrt::Windows::UI::Composition;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wss = winrt::Windows::Storage::Streams;
```

> O bloco `template <> inline constexpr winrt::guid …` tem de ficar **fora** do `namespace styler::tap` (é uma especialização em `winrt::impl`) e **depois** de `winrt/Windows.Foundation.h`.

- [ ] **Step 2: `src/tap/blur_effects.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include <styler/blur.h>
#include <tap/winrt_common.h>

namespace styler::tap {

winrt::Windows::UI::Color ToWinRtColor(const styler::BlurColor& c);

// A 256x256 tiled grayscale noise bitmap, as a stream Composition's
// LoadedImageSurface can read. Cached per density on the calling thread, so
// the 65k-pixel generation happens once per distinct NoiseDensity per XAML
// host thread. Callers get an independent clone (own seek cursor).
// vendor:12412-12470.
wss::IRandomAccessStream CreateNoiseStream(float density);

// --- The five D2D effects the blur graph needs ------------------------------
//
// Composition accepts any object implementing IGraphicsEffect plus
// IGraphicsEffectD2D1Interop as an effect description: the interop interface
// names the D2D CLSID, the property indices, and the sources. Nothing here
// touches D2D itself - these are pure descriptions the compositor realises.
//
// Note the shape every one of them shares, which is NOT obvious:
//   * the interop methods carry NO `override` and NO `IFACEMETHODIMP` -
//     winrt::implements dispatches through a CRTP shim, and `override` fails
//     to compile with C3668;
//   * GetProperty and GetSource take the MIDL ABI pointer types from the SDK
//     header (ABI::Windows::Foundation::IPropertyValue**,
//     awge::IGraphicsEffectSource**), not winrt::impl::abi_t<...>**. The two
//     are binary-identical but distinct C++ types, so the value is detached
//     as abi_t and reinterpret_cast across. Using abi_t in the signature
//     leaves the SDK's pure virtual unimplemented (C2259).
// Both were measured by this plan's compile probe, not assumed.

struct GaussianBlurEffect
    : winrt::implements<GaussianBlurEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    float BlurAmount = 0.0f;
    D2D1_GAUSSIANBLUR_OPTIMIZATION Optimization =
        D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED;
    D2D1_BORDER_MODE BorderMode = D2D1_BORDER_MODE_SOFT;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    winrt::hstring m_name;
};

struct ColorMatrixEffect
    : winrt::implements<ColorMatrixEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    // 5x4, row-major: rows 0-3 scale R/G/B/A, row 4 is the offset.
    // Identity by default.
    std::array<float, 20> Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
                                1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    D2D1_COLORMATRIX_ALPHA_MODE AlphaMode =
        D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    BOOL ClampOutput = FALSE;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    winrt::hstring m_name;
};

struct CompositeEffect
    : winrt::implements<CompositeEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    std::vector<wge::IGraphicsEffectSource> Sources;
    D2D1_COMPOSITE_MODE Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    winrt::hstring m_name;
};

struct FloodEffect : winrt::implements<FloodEffect, wge::IGraphicsEffect,
                                      wge::IGraphicsEffectSource,
                                      awge::IGraphicsEffectD2D1Interop> {
    winrt::Windows::UI::Color Color{};

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    winrt::hstring m_name;
};

struct BorderEffect : winrt::implements<BorderEffect, wge::IGraphicsEffect,
                                       wge::IGraphicsEffectSource,
                                       awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    D2D1_BORDER_EDGE_MODE ExtendX = D2D1_BORDER_EDGE_MODE_WRAP;
    D2D1_BORDER_EDGE_MODE ExtendY = D2D1_BORDER_EDGE_MODE_WRAP;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    winrt::hstring m_name;
};

}  // namespace styler::tap
```

- [ ] **Step 3: `src/tap/blur_effects.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/blur_effects.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace styler::tap {
namespace {

// The two casts every GetProperty / GetSource needs. Kept here so the reason
// lives in one place: cppwinrt's abi_t<T> and the MIDL ABI::...::T the SDK
// interop header declares are the same vtable under two C++ type names.
ABI::Windows::Foundation::IPropertyValue* DetachPropertyValue(
    wf::IInspectable const& boxed) {
    return reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(
        boxed.as<winrt::impl::abi_t<wf::IPropertyValue>>().detach());
}

HRESULT DetachSource(wge::IGraphicsEffectSource const& source,
                     awge::IGraphicsEffectSource** out) {
    if (!source) {
        *out = nullptr;
        return E_BOUNDS;
    }
    winrt::com_ptr<::IUnknown> unknown =
        source.as<::IUnknown>();  // AddRef'd copy we hand over.
    *out = reinterpret_cast<awge::IGraphicsEffectSource*>(unknown.detach());
    return S_OK;
}

}  // namespace

winrt::Windows::UI::Color ToWinRtColor(const styler::BlurColor& c) {
    return winrt::Windows::UI::Color{c.a, c.r, c.g, c.b};
}

// ---------------------------------------------------------------- noise ----

wss::IRandomAccessStream CreateNoiseStream(float density) {
    thread_local float t_cached_density = std::numeric_limits<float>::quiet_NaN();
    thread_local wss::InMemoryRandomAccessStream t_cached{nullptr};

    if (t_cached && density == t_cached_density) {
        return t_cached.CloneStream();
    }

    // 256x256 keeps the tiling seam out of sight at taskbar sizes.
    constexpr int kSize = 256;
    constexpr DWORD kBpp = 32;
    constexpr DWORD kRowSize = kSize * (kBpp / 8);
    constexpr DWORD kDataSize = kRowSize * kSize;

    BITMAPFILEHEADER file_header{
        .bfType = 0x4D42,  // "BM"
        .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + kDataSize,
        .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER),
    };
    BITMAPINFOHEADER info_header{
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = kSize,
        .biHeight = kSize,
        .biPlanes = 1,
        .biBitCount = kBpp,
        .biSizeImage = kDataSize,
    };

    // Density shapes the grey distribution through a power curve; precompute
    // it over the 256 possible samples instead of calling pow 65536 times.
    const float safe_density = std::clamp(density, 0.001f, 1.0f);
    const float exponent = 1.0f / safe_density;
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(
            std::pow(i / 255.0f, exponent) * 255.0f);
    }

    // Fixed seed: the noise must be identical on every thread and every run,
    // or two taskbars would show visibly different grain.
    std::mt19937 rng(0);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> pixels(kDataSize);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        uint8_t grey = lut[static_cast<size_t>(dist(rng))];
        pixels[i] = grey;
        pixels[i + 1] = grey;
        pixels[i + 2] = grey;
        // Fully opaque; NoiseOpacity is applied downstream by the
        // ColorMatrixEffect, so the texture itself stays neutral.
        pixels[i + 3] = 0xFF;
    }

    wss::InMemoryRandomAccessStream stream;
    wss::DataWriter writer(stream);
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&file_header),
        reinterpret_cast<const uint8_t*>(&file_header) + sizeof(file_header)));
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&info_header),
        reinterpret_cast<const uint8_t*>(&info_header) + sizeof(info_header)));
    writer.WriteBytes(pixels);

    // This runs on a XAML UI thread, which is an STA: IAsyncOperation::get()
    // blocks, and C++/WinRT asserts on a blocking wait from an STA in debug
    // builds. Storing into an in-memory stream completes synchronously, so
    // the operation is already finished here and the wait never happens -
    // the guard makes that explicit instead of relying on it silently, and
    // still waits in the (unobserved) case where it did not.
    auto store = writer.StoreAsync();
    if (store.Status() == wf::AsyncStatus::Started) {
        store.get();
    }
    writer.DetachStream();
    stream.Seek(0);

    t_cached = stream;
    t_cached_density = density;
    return t_cached.CloneStream();
}

// ------------------------------------------------------- GaussianBlur ------

HRESULT GaussianBlurEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1GaussianBlur;
    return S_OK;
}

HRESULT GaussianBlurEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
    if (n == L"BlurAmount") {
        *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
        return S_OK;
    }
    if (n == L"Optimization") {
        *index = D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION;
        return S_OK;
    }
    if (n == L"BorderMode") {
        *index = D2D1_GAUSSIANBLUR_PROP_BORDER_MODE;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT GaussianBlurEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 3;
    return S_OK;
}

HRESULT GaussianBlurEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateSingle(BlurAmount));
            return S_OK;
        case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
            *value = DetachPropertyValue(wf::PropertyValue::CreateUInt32(
                static_cast<uint32_t>(Optimization)));
            return S_OK;
        case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
            *value = DetachPropertyValue(wf::PropertyValue::CreateUInt32(
                static_cast<uint32_t>(BorderMode)));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT GaussianBlurEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT GaussianBlurEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

// --------------------------------------------------------- ColorMatrix -----

HRESULT ColorMatrixEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1ColorMatrix;
    return S_OK;
}

HRESULT ColorMatrixEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    if (n == L"ColorMatrix") {
        *index = D2D1_COLORMATRIX_PROP_COLOR_MATRIX;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"AlphaMode") {
        *index = D2D1_COLORMATRIX_PROP_ALPHA_MODE;
        *mapping =
            awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_COLORMATRIX_ALPHA_MODE;
        return S_OK;
    }
    if (n == L"ClampOutput") {
        *index = D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT ColorMatrixEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 3;
    return S_OK;
}

HRESULT ColorMatrixEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_COLORMATRIX_PROP_COLOR_MATRIX:
            *value = DetachPropertyValue(wf::PropertyValue::CreateSingleArray(
                winrt::array_view<const float>(Matrix.data(),
                                               Matrix.data() + Matrix.size())));
            return S_OK;
        case D2D1_COLORMATRIX_PROP_ALPHA_MODE:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(AlphaMode)));
            return S_OK;
        case D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateBoolean(ClampOutput != FALSE));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT ColorMatrixEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT ColorMatrixEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

// ----------------------------------------------------------- Composite -----

HRESULT CompositeEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Composite;
    return S_OK;
}

HRESULT CompositeEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    if (std::wstring_view(name) == L"Mode") {
        *index = D2D1_COMPOSITE_PROP_MODE;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CompositeEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

HRESULT CompositeEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    if (index != D2D1_COMPOSITE_PROP_MODE) {
        return E_BOUNDS;
    }
    *value = DetachPropertyValue(
        wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(Mode)));
    return S_OK;
} catch (...) {
    return winrt::to_hresult();
}

HRESULT CompositeEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index >= Sources.size()) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Sources[index], source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT CompositeEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = static_cast<UINT>(Sources.size());
    return S_OK;
}

// --------------------------------------------------------------- Flood -----

HRESULT FloodEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Flood;
    return S_OK;
}

HRESULT FloodEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    if (std::wstring_view(name) == L"Color") {
        *index = D2D1_FLOOD_PROP_COLOR;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT FloodEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

HRESULT FloodEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    if (index != D2D1_FLOOD_PROP_COLOR) {
        return E_BOUNDS;
    }
    // Straight (non-premultiplied) RGBA, exactly as upstream sends it
    // (vendor:12847-12858). Deviating here changes how the tint composites
    // over the blur, which is the one thing this plan must not do.
    const float rgba[4] = {Color.R / 255.0f, Color.G / 255.0f,
                           Color.B / 255.0f, Color.A / 255.0f};
    *value = DetachPropertyValue(wf::PropertyValue::CreateSingleArray(
        winrt::array_view<const float>(rgba, rgba + 4)));
    return S_OK;
} catch (...) {
    return winrt::to_hresult();
}

HRESULT FloodEffect::GetSource(UINT,
                               awge::IGraphicsEffectSource** source) noexcept {
    if (!source) {
        return E_INVALIDARG;
    }
    *source = nullptr;
    return E_BOUNDS;  // Flood generates; it has no input.
}

HRESULT FloodEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 0;
    return S_OK;
}

// -------------------------------------------------------------- Border -----

HRESULT BorderEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Border;
    return S_OK;
}

HRESULT BorderEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
    if (n == L"ExtendX") {
        *index = D2D1_BORDER_PROP_EDGE_MODE_X;
        return S_OK;
    }
    if (n == L"ExtendY") {
        *index = D2D1_BORDER_PROP_EDGE_MODE_Y;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT BorderEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 2;
    return S_OK;
}

HRESULT BorderEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_BORDER_PROP_EDGE_MODE_X:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(ExtendX)));
            return S_OK;
        case D2D1_BORDER_PROP_EDGE_MODE_Y:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(ExtendY)));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT BorderEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT BorderEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

}  // namespace styler::tap
```

- [ ] **Step 4: CMake**

`src/tap/CMakeLists.txt`: acrescentar `blur_effects.cpp` ao alvo `taskbar_styler_tap` (o MODULE, não o lib estático — depende de WinRT).

**Test cycle:**

Esta task não ganha teste doctest: tudo aqui depende de `winrt/` e mora no MODULE que os testes não linkam (`src/tap/CMakeLists.txt` documenta a divisão). O ciclo é:

1. **Build limpo.** `cmake --build build`. Zero warnings novos com `/W4`. Se aparecer **C3668**, algum método ganhou `override` — remova-o (ver o comentário do header). Se aparecer **C2259**, alguma assinatura usou `winrt::impl::abi_t<...>**` em vez do tipo MIDL.
2. **Compilação isolada do TU**, para separar um erro deste arquivo de um erro do resto do DLL. Este comando foi executado ao escrever o plano contra exatamente este código e saiu com **0 erros e 0 warnings** (fora o `C4002` do próprio SDK, em `Windows.UI.Xaml.Media.Animation.0.h`):

```
cl /std:c++20 /EHsc /permissive- /W4 /utf-8 /DWINRT_LEAN_AND_MEAN /bigobj /I src /c src/tap/blur_effects.cpp /Fo:build/probe_blur_effects.obj
```
3. **Smoke ao vivo:** `load` + `apply TranslucentTaskbar`. O comportamento **não muda** (nada consome os efeitos ainda); o que se verifica é que o DLL maior continua carregando: log com `initialized for thread N`, `subscription started`, sem `ERR`.

**Commit:** `feat(tap): efeitos D2D do WindhawkBlur sobre IGraphicsEffectD2D1Interop do SDK`

---

### Task 4: `XamlBlurBrush` e o fim da aproximação

O brush de verdade, e a única mudança visual que o usuário vê neste plano. O `AcrylicBrush` continua no código como fallback, agora com um caminho explícito e contado.

**Files:**
- Create: `src/tap/blur_brush.h`, `src/tap/blur_brush.cpp`
- Modify: `src/tap/property_setter.h`, `src/tap/property_setter.cpp`, `src/tap/style_engine.cpp`, `src/tap/CMakeLists.txt`, `docs/smoke-test.md`, `README.md`

**Interfaces:**
- Consumes: `styler::BlurSpec` (Task 2), os cinco efeitos e `CreateNoiseStream` (Task 3), `ResolvedSetter`, `CachedSetter`, `SetCustom` (Plano 3).
- Produces:
  - `wuxm::Brush styler::tap::MakeBlurBrush(wux::UIElement const& element, const styler::BlurSpec& spec)` — `nullptr` quando não deu para criar (o chamador cai no `AcrylicBrush`).
  - `ResolvedSetter::blur` (`const styler::BlurSpec*`) — aponta para o spec dentro do `PreparedStyle`, que vive enquanto o `ResolvedTheme` viver (o cache já segura um `shared_ptr` para ele).
  - `EngineStats::blur_brushes` e `EngineStats::blur_fallbacks`.

- [ ] **Step 1: `src/tap/blur_brush.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <styler/blur.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The real `<WindhawkBlur .../>`: a XamlCompositionBrushBase whose
// CompositionBrush is an effect graph over the window backdrop (blur, then
// optional saturation, luminosity and noise, then the tint composited on
// top). Ported from upstream's XamlBlurBrush (vendor:13356-13905), which is
// itself TranslucentTB's.
//
// Deliberately NOT ported: the energy-saver / "transparency effects off"
// switch to a flat fallback brush. Upstream's own ShouldUseFallback returns
// false outright when the markup declares no FallbackColor
// (vendor:13831-13842), and no `<WindhawkBlur>` in the 55 shipped themes
// declares one (272 tags measured) - so that whole path, including its
// RegNotifyChangeKeyValue watch on the Power key, is unreachable for every
// theme this project ships. `FallbackColor` is still parsed and is still the
// color used when the effect graph cannot be built at all.
class XamlBlurBrush : public wuxm::XamlCompositionBrushBaseT<XamlBlurBrush> {
   public:
    XamlBlurBrush(wux::UIElement const& element, const styler::BlurSpec& spec);
    ~XamlBlurBrush();

    void OnConnected();
    void OnDisconnected();

   private:
    wuc::CompositionBrush CreateEffectBrush();
    wuc::CompositionBrush CreateFallbackBrush();
    void RefreshThemeTint();
    void RefreshBrush();

    wuc::Compositor m_compositor{nullptr};
    styler::BlurSpec m_spec;
    winrt::Windows::UI::Color m_tint{};
    // Set only when the markup used TintColor="{ThemeResource Key}": a
    // SolidColorBrush bound to that key, parked in the element's Resources so
    // XAML keeps re-evaluating it across light/dark switches, and watched so
    // the effect graph is rebuilt when it changes.
    wuxm::SolidColorBrush m_tint_proxy{nullptr};
    winrt::weak_ref<wux::FrameworkElement> m_proxy_owner;
    winrt::hstring m_proxy_key;
    long long m_tint_changed_token = 0;
};

// Builds a XamlBlurBrush for `element`, or returns nullptr when it cannot be
// built (no Compositor for this element, or the effect factory refused the
// graph). Never throws: the caller is inside the style engine's per-property
// try, but a null return is the documented way to ask for the AcrylicBrush
// fallback, and distinguishing "failed" from "threw" would not change what
// the caller does.
wuxm::Brush MakeBlurBrush(wux::UIElement const& element,
                          const styler::BlurSpec& spec);

}  // namespace styler::tap
```

- [ ] **Step 2: `src/tap/blur_brush.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/blur_brush.h>

#include <algorithm>
#include <atomic>
#include <string>

#include <tap/blur_effects.h>
#include <tap/log.h>

namespace styler::tap {
namespace {

// Rec. 709 luma coefficients - the same ones upstream uses for both the
// saturation and the luminosity matrices (vendor:13690-13693).
constexpr float kLumaR = 0.2126f;
constexpr float kLumaG = 0.7152f;
constexpr float kLumaB = 0.0722f;

std::atomic<uint64_t> g_proxy_counter{0};

}  // namespace

XamlBlurBrush::XamlBlurBrush(wux::UIElement const& element,
                             const styler::BlurSpec& spec)
    : m_compositor(
          wuxh::ElementCompositionPreview::GetElementVisual(element).Compositor()),
      m_spec(spec),
      m_tint(ToWinRtColor(spec.tint)) {
    if (m_spec.tint_theme_resource.empty()) {
        return;
    }
    auto fe = element.try_as<wux::FrameworkElement>();
    if (!fe) {
        STYLER_LOG(LogLevel::Error,
                   L"blur: theme resource tint needs a FrameworkElement");
        return;
    }

    // XAML has no API to evaluate a ThemeResource by key, so park a brush
    // that IS bound to it in the element's own ResourceDictionary and read
    // its Color. XAML then keeps it up to date across light/dark switches
    // for free, and the property-changed callback below turns that into a
    // rebuild of the effect graph.
    std::wstring xaml =
        L"<SolidColorBrush xmlns=\"http://schemas.microsoft.com/winfx/2006/"
        L"xaml/presentation\" Color=\"{ThemeResource " +
        m_spec.tint_theme_resource + L"}\"/>";
    try {
        m_tint_proxy = wux::Markup::XamlReader::Load(winrt::hstring(xaml))
                           .try_as<wuxm::SolidColorBrush>();
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur: proxy brush for %s failed 0x%08X",
                   m_spec.tint_theme_resource.c_str(),
                   static_cast<unsigned>(ex.code()));
        return;
    } catch (...) {
        return;
    }
    if (!m_tint_proxy) {
        return;
    }

    try {
        m_proxy_key = winrt::hstring(L"__TsBlurProxy_" +
                                     std::to_wstring(++g_proxy_counter));
        fe.Resources().Insert(winrt::box_value(m_proxy_key), m_tint_proxy);
        m_proxy_owner = fe;
        m_tint_changed_token = m_tint_proxy.RegisterPropertyChangedCallback(
            wuxm::SolidColorBrush::ColorProperty(),
            [weak = get_weak()](wux::DependencyObject const&,
                                wux::DependencyProperty const&) {
                try {
                    if (auto self = weak.get()) {
                        self->RefreshBrush();
                    }
                } catch (winrt::hresult_error const&) {
                } catch (...) {
                }
            });
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur: proxy registration failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
}

XamlBlurBrush::~XamlBlurBrush() {
    try {
        if (m_tint_proxy && m_tint_changed_token) {
            m_tint_proxy.UnregisterPropertyChangedCallback(
                wuxm::SolidColorBrush::ColorProperty(), m_tint_changed_token);
        }
        // Leaving the proxy key behind would grow the element's
        // ResourceDictionary by one entry per apply/reset cycle.
        if (auto owner = m_proxy_owner.get(); owner && !m_proxy_key.empty()) {
            owner.Resources().Remove(winrt::box_value(m_proxy_key));
        }
    } catch (...) {
        // A destructor is the one place that must never let anything out.
    }
}

void XamlBlurBrush::RefreshThemeTint() {
    if (!m_tint_proxy) {
        return;
    }
    m_tint = m_tint_proxy.Color();
    // TintOpacity, when the author wrote one, wins over the alpha the theme
    // resource carries - upstream does the same (vendor:13818-13826).
    if (m_spec.tint_opacity) {
        m_tint.A = *m_spec.tint_opacity;
    }
}

void XamlBlurBrush::OnConnected() {
    try {
        if (CompositionBrush()) {
            return;
        }
        RefreshThemeTint();
        wuc::CompositionBrush brush{nullptr};
        try {
            brush = CreateEffectBrush();
        } catch (winrt::hresult_error const& ex) {
            STYLER_LOG(LogLevel::Error, L"blur: effect graph failed 0x%08X",
                       static_cast<unsigned>(ex.code()));
        }
        if (!brush) {
            brush = CreateFallbackBrush();
        }
        CompositionBrush(brush);
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
}

void XamlBlurBrush::OnDisconnected() {
    try {
        if (auto brush = CompositionBrush()) {
            brush.Close();
            CompositionBrush(nullptr);
        }
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
}

void XamlBlurBrush::RefreshBrush() {
    // Only when already connected: rebuilding a disconnected brush would
    // create a composition object nothing will ever show or close.
    if (!CompositionBrush()) {
        return;
    }
    OnDisconnected();
    OnConnected();
}

wuc::CompositionBrush XamlBlurBrush::CreateFallbackBrush() {
    return m_compositor.CreateColorBrush(
        m_spec.fallback_color ? ToWinRtColor(*m_spec.fallback_color) : m_tint);
}

wuc::CompositionBrush XamlBlurBrush::CreateEffectBrush() {
    auto backdrop = m_compositor.CreateBackdropBrush();

    // 1. Blur the backdrop.
    auto blur = winrt::make_self<GaussianBlurEffect>();
    blur->Source = wuc::CompositionEffectSourceParameter(L"backdrop");
    blur->BlurAmount = m_spec.blur_amount;
    blur->Name(L"BlurEffect");
    wge::IGraphicsEffectSource top = *blur;

    // 2. Saturation, as a lerp between luminance and identity.
    if (m_spec.tint_saturation && *m_spec.tint_saturation != 1.0f) {
        // Parenthesized: <windows.h> defines a `max` macro and this TU
        // does not define NOMINMAX (measured: C2589 without the parens).
        const float s = (std::max)(*m_spec.tint_saturation, 0.0f);
        const float inv = 1.0f - s;
        auto sat = winrt::make_self<ColorMatrixEffect>();
        sat->Source = top;
        auto& m = sat->Matrix;
        m = {inv * kLumaR + s, inv * kLumaR,     inv * kLumaR,     0.0f,
             inv * kLumaG,     inv * kLumaG + s, inv * kLumaG,     0.0f,
             inv * kLumaB,     inv * kLumaB,     inv * kLumaB + s, 0.0f,
             0.0f,             0.0f,             0.0f,             1.0f,
             0.0f,             0.0f,             0.0f,             0.0f};
        sat->Name(L"SaturationEffect");
        top = *sat;
    }

    // 3. Luminosity: pull each pixel's luma towards the tint's, by `op`.
    if (m_spec.tint_luminosity_opacity && *m_spec.tint_luminosity_opacity > 0.0f) {
        const float op = std::clamp(*m_spec.tint_luminosity_opacity, 0.0f, 1.0f);
        const float tint_luma = (m_tint.R / 255.0f) * kLumaR +
                                (m_tint.G / 255.0f) * kLumaG +
                                (m_tint.B / 255.0f) * kLumaB;
        auto lum = winrt::make_self<ColorMatrixEffect>();
        lum->Source = top;
        auto& m = lum->Matrix;
        m = {1.0f - kLumaR * op, -(kLumaR * op),     -(kLumaR * op),     0.0f,
             -(kLumaG * op),     1.0f - kLumaG * op, -(kLumaG * op),     0.0f,
             -(kLumaB * op),     -(kLumaB * op),     1.0f - kLumaB * op, 0.0f,
             0.0f,               0.0f,               0.0f,               1.0f,
             tint_luma * op,     tint_luma * op,     tint_luma * op,     0.0f};
        lum->Name(L"LuminosityBlend");
        top = *lum;
    }

    // 4. Noise: a wrapped tile, scaled down to NoiseOpacity, over the stack.
    wuc::CompositionSurfaceBrush noise_brush{nullptr};
    if (m_spec.noise_opacity && *m_spec.noise_opacity > 0.0f) {
        auto stream = CreateNoiseStream(m_spec.noise_density.value_or(1.0f));
        auto surface = wuxm::LoadedImageSurface::StartLoadFromStream(stream);
        noise_brush = m_compositor.CreateSurfaceBrush(surface);
        noise_brush.Stretch(wuc::CompositionStretch::None);

        auto border = winrt::make_self<BorderEffect>();
        border->Source = wuc::CompositionEffectSourceParameter(L"NoiseSource");
        border->Name(L"NoiseTile");

        const float n = std::clamp(*m_spec.noise_opacity, 0.0f, 1.0f);
        auto opacity = winrt::make_self<ColorMatrixEffect>();
        opacity->Source = *border;
        // Scale every channel, alpha included: the composite below blends in
        // premultiplied space.
        opacity->Matrix = {n,    0.0f, 0.0f, 0.0f, 0.0f, n,    0.0f, 0.0f,
                           0.0f, 0.0f, n,    0.0f, 0.0f, 0.0f, 0.0f, n,
                           0.0f, 0.0f, 0.0f, 0.0f};
        opacity->Name(L"NoiseOpacityEffect");

        auto noise_composite = winrt::make_self<CompositeEffect>();
        noise_composite->Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;
        noise_composite->Sources.push_back(top);
        noise_composite->Sources.push_back(*opacity);
        noise_composite->Name(L"NoiseComposite");
        top = *noise_composite;
    }

    // 5. The tint, flooded over everything.
    auto flood = winrt::make_self<FloodEffect>();
    flood->Color = m_tint;
    flood->Name(L"FloodEffect");

    auto composite = winrt::make_self<CompositeEffect>();
    composite->Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;
    composite->Sources.push_back(top);
    composite->Sources.push_back(*flood);

    auto factory = m_compositor.CreateEffectFactory(*composite);
    auto brush = factory.CreateBrush();
    brush.SetSourceParameter(L"backdrop", backdrop);
    if (noise_brush) {
        brush.SetSourceParameter(L"NoiseSource", noise_brush);
    }
    return brush;
}

wuxm::Brush MakeBlurBrush(wux::UIElement const& element,
                          const styler::BlurSpec& spec) {
    try {
        if (!element) {
            return nullptr;
        }
        return winrt::make<XamlBlurBrush>(element, spec);
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur brush creation failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
        return nullptr;
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"blur brush creation failed");
        return nullptr;
    }
}

}  // namespace styler::tap
```

> `winrt_common.h` precisa de `#include <winrt/Windows.UI.Xaml.Markup.h>` (já tem) e de `wuxm::LoadedImageSurface`, que vem de `winrt/Windows.UI.Xaml.Media.h` (já tem).

- [ ] **Step 3: `ResolvedSetter` carrega o spec**

`src/tap/property_setter.h`:

```cpp
struct ResolvedSetter {
    wux::DependencyProperty property{nullptr};
    wf::IInspectable value;  // Null when `clear` is set.
    bool clear = false;      // `Prop:=` with an empty value clears the property.
    // Non-null when the style's value was a `<WindhawkBlur .../>`. `value`
    // then holds the shared AcrylicBrush fallback, which IS safe to share
    // across elements; the real brush is NOT (it captures the element's
    // Compositor and parks a proxy in its Resources), so the engine builds
    // one per element from this spec instead of caching it here. Points into
    // the PreparedStyle inside the ResolvedTheme, which the setter cache
    // already keeps alive through its own shared_ptr.
    const styler::BlurSpec* blur = nullptr;
};
```

`src/tap/property_setter.cpp`, no fim de `ResolveSetter`, antes do `return`:

```cpp
    // The blur spec rides along with the resolved property; the value stays
    // the AcrylicBrush the markup already parsed into, so a caller that does
    // not know about blur still gets something drawable.
    out.blur = style.blur ? &*style.blur : nullptr;
```

- [ ] **Step 4: o motor cria um brush por elemento**

Em `src/tap/style_engine.cpp`, trocar a linha

```cpp
                PropertyState& prop = bucket.properties[setter->property];
                prop.values[style.visual_state] = setter->clear ? nullptr : setter->value;
```

por

```cpp
                PropertyState& prop = bucket.properties[setter->property];
                wf::IInspectable value = setter->clear ? nullptr : setter->value;
                if (setter->blur) {
                    // One brush per element: a XamlBlurBrush holds the
                    // element's Compositor and inserts a proxy key into its
                    // Resources, so the setter cache's shared value would
                    // wire every matched element to the first one's visual.
                    // The AcrylicBrush in setter->value is the documented
                    // fallback (src/core/blur_rewrite.h) and is shareable.
                    if (auto brush = MakeBlurBrush(element, *setter->blur)) {
                        value = brush;
                        ++t_stats.blur_brushes;
                    } else {
                        ++t_stats.blur_fallbacks;
                        STYLER_LOG(LogLevel::Error,
                                   L"blur fell back to AcrylicBrush on %s",
                                   type.c_str());
                    }
                }
                prop.values[style.visual_state] = value;
```

e incluir `<tap/blur_brush.h>` no topo do arquivo.

`src/tap/style_engine.h`, em `EngineStats`:

```cpp
    size_t blur_brushes = 0;    // Real WindhawkBlur brushes created.
    size_t blur_fallbacks = 0;  // Blurs that fell back to AcrylicBrush.
```

E a linha do primeiro dreno em `src/tap/release_queue.cpp`, que já imprime `EngineStats`, ganha os dois números:

```cpp
        STYLER_LOG(LogLevel::Info,
                   L"initial apply: %zu elements, %zu properties, %zu failed, "
                   L"%zu deferred, %zu blur brushes, %zu blur fallbacks",
                   stats.styled_elements, stats.applied_properties,
                   stats.failed_styles, stats.deferred_visual_state_styles,
                   stats.blur_brushes, stats.blur_fallbacks);
```

(ajustar ao texto exato que estiver lá; o que importa é que os dois contadores apareçam nessa linha.)

- [ ] **Step 5: o restore fecha o brush**

Em `Unapply` (`src/tap/style_engine.cpp`), depois de `prop.applied = false; prop.original = nullptr;`:

```cpp
    // A XamlBlurBrush that is no longer on any property still holds a
    // composition brush and a proxy entry in the element's Resources until
    // its last reference goes. XAML drops its own reference when the value is
    // replaced; `prop.values` is the only other holder, and it dies with the
    // ElementState. Nothing to close by hand here - but DO NOT cache the
    // brush anywhere longer-lived than that, which is why MakeBlurBrush is
    // called per element and never memoized.
```

Isto é comentário, não código: a nota existe porque a tentação de cachear o brush é exatamente o bug que ela evita.

- [ ] **Step 6: CMake e docs**

`src/tap/CMakeLists.txt`: `blur_brush.cpp` no alvo `taskbar_styler_tap`.

`docs/smoke-test.md`, novo item na seção "Aplicar e desfazer":

```
10. **Blur real.** `apply FrostyGlass` (ou `TranslucentTaskbar`). No log:
    `N blur brushes, 0 blur fallbacks`. Visual: a taskbar borra o papel de
    parede atrás dela e o borrão **acompanha** uma janela arrastada por baixo
    — o `AcrylicBrush` do Plano 3 também é translúcido, então a prova é o
    movimento, não a transparência.
11. **Tint por tema.** `apply Command_Center` (usa
    `TintColor="{ThemeResource SystemChromeMediumColor}"`). Alterne
    Configurações → Personalização → Cores entre claro e escuro: o tom da
    taskbar acompanha, sem reaplicar o tema.
12. **Ruído.** `apply Luminosity_variant_Classic`: o grão fino é visível
    contra um papel de parede liso.
```

`README.md`: onde diz que o blur é aproximado por `AcrylicBrush`, trocar por "blur de composição real; o `AcrylicBrush` é o fallback quando a composição não está disponível".

**Test cycle:**

1. Build + `ctest` (os testes de core continuam verdes; nada aqui é testável off-target).
2. **Smoke ao vivo, obrigatório** (esta é a task que muda pixel):
   - `build\src\cli\taskbar-styler.exe apply TranslucentTaskbar` — log com `2 blur brushes, 0 blur fallbacks`; arraste uma janela sob a taskbar e confirme que o borrão acompanha.
   - `apply Command_Center` — alterne claro/escuro; o log mostra uma linha de `blur` por rebuild (o callback do proxy) e a cor muda.
   - `apply Luminosity_variant_Classic` — grão visível.
   - `reset` — taskbar padrão, `restored N`, `held=0`.
   - Zero `ERR` no log em todo o ciclo; o PID do explorer no fim é o mesmo do início.
3. **Se aparecer `blur fell back to AcrylicBrush`**, registre no ledger *em qual elemento* e continue: o fallback é o comportamento projetado, mas uma queda sistemática significa que o grafo foi recusado e precisa de investigação antes do merge.

**Commit:** `feat(tap): WindhawkBlur real com XamlCompositionBrushBase`

---

### Task 5: o avaliador de `{{...}}` no core

`{{Var}}` **não** é uma referência a variável: é uma linguagem de expressão. No corpus há aritmética, `min`/`max`, ternário, comparação e literais de crase (medição na tabela de Fatos). Esta task entrega o avaliador inteiro como parte pura, com o corpus como teste — nada dela toca XAML, e é por isso que ela vem antes da parte viva.

**Files:**
- Create: `src/core/include/styler/style_expression.h`, `src/core/style_expression.cpp`, `tests/core/test_style_expression.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `src/core/include/styler/matcher.h`, `src/core/matcher.cpp`

**Interfaces:**
- Consumes: nada (parte pura nova).
- Produces:
  - `struct styler::StyleVariableValue { std::wstring text; std::optional<double> number; bool substitutable; }`.
  - `using styler::StyleVariableLookup = std::function<const StyleVariableValue*(std::wstring_view)>`.
  - `std::optional<std::wstring> styler::ExpandStyleVariables(std::wstring_view, const StyleVariableLookup&, std::vector<std::wstring>* deps)` — `nullopt` = **pule este estilo**.
  - `std::wstring styler::FormatDoubleInvariant(double)`.
  - `PreparedStyle::dynamic` (`bool`) — o estilo tem `{{…}}` e é resolvido por elemento em vez de na preparação.

> **Gramática, verbatim do readme do upstream (`vendor:375-410`)**, para quem implementar não precisar inferi-la: números; literais entre crases (crase dobrada escapa uma crase); referências a variável; `+ - * /` e os unários `+ -`; `< <= == >= > !=` valendo 1 ou 0; `cond ? a : b`; `min(a, b)` e `max(a, b)`; parênteses. Os pares de chave casam **do mais interno para fora**. Dentro de uma expressão, variável indefinida vale string vazia; um `{{Var}}` **puro** (só o identificador) de variável indefinida ou não-substituível **pula o estilo**.

- [ ] **Step 1: `src/core/include/styler/style_expression.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace styler {

// One captured style variable, as the consumer side sees it.
struct StyleVariableValue {
    // Text form, used by a bare `{{Var}}` substitution and by string
    // comparisons. Empty means "captured, but with no value".
    std::wstring text;
    // Engaged when the captured value is numeric, which is what arithmetic,
    // the relational operators and min/max require.
    std::optional<double> number;
    // False for opaque captures (a brush, a thickness): the variable exists,
    // but substituting it would emit a class name into the XAML. A bare
    // `{{Var}}` on such a variable skips the style.
    bool substitutable = false;
};

// Returns the variable, or nullptr when no capture currently defines it.
using StyleVariableLookup =
    std::function<const StyleVariableValue*(std::wstring_view)>;

// Expands every `{{ ... }}` in `input`. Brace pairs are matched innermost
// first, so `{{{x}}}` is a literal `{`, a substitution, and a literal `}`.
//
// Returns nullopt when the style must be SKIPPED rather than applied:
//   * a bare `{{Var}}` whose variable is undefined or not substitutable;
//   * a malformed expression, an unmatched `}}`, a type error (arithmetic on
//     a string), or a non-finite result.
// Inside a larger expression an undefined variable is the empty string, so a
// theme can supply its own default with the conditional operator.
//
// Every variable name the input referenced is appended to `deps` when `deps`
// is non-null, including names that turned out undefined - the caller
// registers them so the style is recomputed once something captures them.
// Pure: no Windows, no XAML.
std::optional<std::wstring> ExpandStyleVariables(
    std::wstring_view input, const StyleVariableLookup& lookup,
    std::vector<std::wstring>* deps);

// Formats a double the way a XAML attribute wants it: invariant culture,
// shortest representation that round-trips, no trailing zeros.
std::wstring FormatDoubleInvariant(double value);

}  // namespace styler
```

- [ ] **Step 2: `src/core/style_expression.cpp`**

Este arquivo foi **compilado e executado** ao escrever o plano (`cl /std:c++20 /EHsc /permissive- /W4 /utf-8`), contra os 23 casos do Step 4: 0 erros, 0 warnings, 0 falhas.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/style_expression.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace styler {
namespace {

// A value while an expression is being evaluated: either a number or a
// string. Number literals and numeric variables produce numbers; backtick
// literals and string-typed variables produce strings.
struct ExprValue {
    std::optional<double> number;
    std::wstring text;

    static ExprValue Number(double d) { return {d, std::wstring()}; }
    static ExprValue String(std::wstring s) {
        return {std::nullopt, std::move(s)};
    }
    bool IsNumber() const { return number.has_value(); }
};

class EvalError : public std::runtime_error {
   public:
    explicit EvalError(const char* what) : std::runtime_error(what) {}
};

// Recursive-descent evaluator for one `{{ ... }}` body. Operands: number
// literals, backtick-delimited string literals, variable references and
// parenthesised subexpressions. Operators: binary + - * /, unary + -, the
// comparisons < <= == >= > != (yielding 1 or 0), the conditional
// `cond ? a : b`, and the two-argument min(a, b) / max(a, b). Standard
// precedence. Arithmetic, the unary sign, the relational comparisons and
// min/max require numbers; == and != compare two numbers or two strings and
// treat a number-versus-string mismatch as unequal; the conditional's
// condition must be numeric but its branches need not be.
// Ported from upstream vendor:16220-16650.
class Evaluator {
   public:
    Evaluator(std::wstring_view text, const StyleVariableLookup& lookup,
              std::vector<std::wstring>* deps)
        : text_(text), lookup_(lookup), deps_(deps) {}

    // The text form of the result. Throws EvalError on any failure,
    // including a non-finite number: NaN and infinity cannot be written into
    // a XAML attribute meaningfully, and NaN would also break the
    // "did the value change?" check the propagation side does.
    std::wstring Evaluate() {
        pos_ = 0;
        SkipSpace();
        ExprValue v = ParseExpression();
        SkipSpace();
        if (pos_ != text_.size()) {
            throw EvalError("trailing text in expression");
        }
        if (!v.IsNumber()) {
            return v.text;
        }
        if (!std::isfinite(*v.number)) {
            throw EvalError("non-finite result");
        }
        return FormatDoubleInvariant(*v.number);
    }

   private:
    static bool IsSpace(wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    }
    static bool IsIdentStart(wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
               c == L'_';
    }
    static bool IsIdentCont(wchar_t c) {
        return IsIdentStart(c) || (c >= L'0' && c <= L'9');
    }

    void SkipSpace() {
        while (pos_ < text_.size() && IsSpace(text_[pos_])) {
            ++pos_;
        }
    }
    bool Take(wchar_t c) {
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool Take(std::wstring_view s) {
        SkipSpace();
        if (text_.compare(pos_, s.size(), s) == 0) {
            pos_ += s.size();
            return true;
        }
        return false;
    }
    wchar_t Peek() const { return pos_ < text_.size() ? text_[pos_] : L'\0'; }

    const StyleVariableValue* Lookup(std::wstring_view name) const;

    static double RequireNumber(const ExprValue& v) {
        if (!v.IsNumber()) {
            throw EvalError("expected a number");
        }
        return *v.number;
    }

    static bool ValuesEqual(const ExprValue& a, const ExprValue& b) {
        if (a.IsNumber() != b.IsNumber()) {
            return false;  // A number never equals a string.
        }
        return a.IsNumber() ? *a.number == *b.number : a.text == b.text;
    }

    ExprValue ParseExpression() { return ParseTernary(); }

    ExprValue ParseTernary() {
        ExprValue cond = ParseEquality();
        SkipSpace();
        if (Peek() != L'?') {
            return cond;
        }
        ++pos_;
        double c = RequireNumber(cond);
        ExprValue then_value = ParseExpression();
        if (!Take(L':')) {
            throw EvalError("expected ':' in conditional");
        }
        ExprValue else_value = ParseTernary();
        return c != 0.0 ? then_value : else_value;
    }

    ExprValue ParseEquality() {
        ExprValue v = ParseRelational();
        for (;;) {
            SkipSpace();
            if (Take(L"==")) {
                v = ExprValue::Number(ValuesEqual(v, ParseRelational()) ? 1.0
                                                                       : 0.0);
            } else if (Take(L"!=")) {
                v = ExprValue::Number(ValuesEqual(v, ParseRelational()) ? 0.0
                                                                       : 1.0);
            } else {
                return v;
            }
        }
    }

    ExprValue ParseRelational() {
        ExprValue v = ParseAdditive();
        for (;;) {
            SkipSpace();
            double lhs = 0.0;
            if (Take(L"<=")) {
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs <= RequireNumber(ParseAdditive()) ? 1.0
                                                                           : 0.0);
            } else if (Take(L">=")) {
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs >= RequireNumber(ParseAdditive()) ? 1.0
                                                                           : 0.0);
            } else if (Peek() == L'<') {
                ++pos_;
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs < RequireNumber(ParseAdditive()) ? 1.0
                                                                          : 0.0);
            } else if (Peek() == L'>') {
                ++pos_;
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs > RequireNumber(ParseAdditive()) ? 1.0
                                                                          : 0.0);
            } else {
                return v;
            }
        }
    }

    ExprValue ParseAdditive() {
        ExprValue v = ParseTerm();
        for (;;) {
            SkipSpace();
            wchar_t c = Peek();
            if (c != L'+' && c != L'-') {
                return v;
            }
            ++pos_;
            double lhs = RequireNumber(v);
            double rhs = RequireNumber(ParseTerm());
            v = ExprValue::Number(c == L'+' ? lhs + rhs : lhs - rhs);
        }
    }

    ExprValue ParseTerm() {
        ExprValue v = ParseUnary();
        for (;;) {
            SkipSpace();
            wchar_t c = Peek();
            if (c != L'*' && c != L'/') {
                return v;
            }
            ++pos_;
            double lhs = RequireNumber(v);
            double rhs = RequireNumber(ParseUnary());
            if (c == L'/' && rhs == 0.0) {
                throw EvalError("division by zero");
            }
            v = ExprValue::Number(c == L'*' ? lhs * rhs : lhs / rhs);
        }
    }

    ExprValue ParseUnary() {
        SkipSpace();
        wchar_t c = Peek();
        if (c == L'-') {
            ++pos_;
            return ExprValue::Number(-RequireNumber(ParseUnary()));
        }
        if (c == L'+') {
            ++pos_;
            return ExprValue::Number(RequireNumber(ParseUnary()));
        }
        return ParsePrimary();
    }

    ExprValue ParsePrimary() {
        SkipSpace();
        if (pos_ >= text_.size()) {
            throw EvalError("unexpected end of expression");
        }
        wchar_t c = text_[pos_];

        if (c == L'(') {
            ++pos_;
            ExprValue v = ParseExpression();
            if (!Take(L')')) {
                throw EvalError("expected ')'");
            }
            return v;
        }

        if (c == L'`') {
            // A doubled backtick encodes one literal backtick.
            ++pos_;
            std::wstring out;
            for (;;) {
                if (pos_ >= text_.size()) {
                    throw EvalError("unterminated string literal");
                }
                if (text_[pos_] == L'`') {
                    if (pos_ + 1 < text_.size() && text_[pos_ + 1] == L'`') {
                        out += L'`';
                        pos_ += 2;
                        continue;
                    }
                    ++pos_;
                    return ExprValue::String(std::move(out));
                }
                out += text_[pos_++];
            }
        }

        if ((c >= L'0' && c <= L'9') || c == L'.') {
            size_t start = pos_;
            while (pos_ < text_.size() &&
                   ((text_[pos_] >= L'0' && text_[pos_] <= L'9') ||
                    text_[pos_] == L'.')) {
                ++pos_;
            }
            std::wstring_view digits = text_.substr(start, pos_ - start);
            std::string narrow(digits.begin(), digits.end());
            double out = 0.0;
            auto [ptr, ec] = std::from_chars(
                narrow.data(), narrow.data() + narrow.size(), out);
            if (ec != std::errc{} || ptr != narrow.data() + narrow.size()) {
                throw EvalError("bad number literal");
            }
            return ExprValue::Number(out);
        }

        if (IsIdentStart(c)) {
            size_t start = pos_;
            while (pos_ < text_.size() && IsIdentCont(text_[pos_])) {
                ++pos_;
            }
            std::wstring_view name = text_.substr(start, pos_ - start);
            SkipSpace();
            if (Peek() == L'(') {
                if (name != L"min" && name != L"max") {
                    throw EvalError("unknown function");
                }
                ++pos_;
                ExprValue a = ParseExpression();
                if (!Take(L',')) {
                    throw EvalError("expected ',' in min/max");
                }
                ExprValue b = ParseExpression();
                if (!Take(L')')) {
                    throw EvalError("expected ')' after min/max");
                }
                double x = RequireNumber(a);
                double y = RequireNumber(b);
                return ExprValue::Number(name == L"min" ? (x < y ? x : y)
                                                        : (x > y ? x : y));
            }
            const StyleVariableValue* var = Lookup(name);
            if (!var) {
                // Undefined inside an expression is the empty string, so a
                // theme can default with `{{w == `` ? 80 : w}}`. The numeric
                // operators then fail on it, which skips the style rather
                // than pretending it is 0.
                return ExprValue::String(std::wstring());
            }
            if (var->number) {
                return ExprValue::Number(*var->number);
            }
            return ExprValue::String(var->text);
        }

        throw EvalError("unexpected character");
    }

    std::wstring_view text_;
    const StyleVariableLookup& lookup_;
    std::vector<std::wstring>* deps_;
    size_t pos_ = 0;
};

void AddDependency(std::vector<std::wstring>* deps, std::wstring_view name) {
    if (!deps) {
        return;
    }
    std::wstring owned(name);
    if (std::find(deps->begin(), deps->end(), owned) == deps->end()) {
        deps->push_back(std::move(owned));
    }
}

const StyleVariableValue* Evaluator::Lookup(std::wstring_view name) const {
    AddDependency(deps_, name);
    return lookup_ ? lookup_(name) : nullptr;
}

// The whole body is a single identifier and nothing else: the "bare
// reference" form, which substitutes the captured text verbatim and which an
// undefined variable must skip rather than blank out. Empty when it is not.
std::wstring_view BareIdentifier(std::wstring_view body) {
    auto is_space = [](wchar_t c) { return c == L' ' || c == L'	'; };
    auto is_start = [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
    };
    auto is_cont = [&](wchar_t c) {
        return is_start(c) || (c >= L'0' && c <= L'9');
    };
    size_t i = 0;
    while (i < body.size() && is_space(body[i])) {
        ++i;
    }
    size_t start = i;
    if (i >= body.size() || !is_start(body[i])) {
        return {};
    }
    while (i < body.size() && is_cont(body[i])) {
        ++i;
    }
    size_t end = i;
    while (i < body.size() && is_space(body[i])) {
        ++i;
    }
    return i == body.size() ? body.substr(start, end - start)
                            : std::wstring_view{};
}

}  // namespace

std::wstring FormatDoubleInvariant(double value) {
    // to_chars with no format flag gives the shortest representation that
    // round-trips, always with '.' as the separator - which is exactly what a
    // XAML attribute needs and what the process locale must not be allowed to
    // change.
    std::array<char, 64> buffer{};
    auto [ptr, ec] =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (ec != std::errc{}) {
        return L"0";
    }
    return std::wstring(buffer.data(), ptr);
}

std::optional<std::wstring> ExpandStyleVariables(
    std::wstring_view input, const StyleVariableLookup& lookup,
    std::vector<std::wstring>* deps) {
    std::wstring result(input);
    size_t scan_from = 0;

    for (;;) {
        // Leftmost `}}` at or after scan_from...
        size_t close = std::wstring::npos;
        for (size_t i = scan_from; i + 1 < result.size(); ++i) {
            if (result[i] == L'}' && result[i + 1] == L'}') {
                close = i;
                break;
            }
        }
        if (close == std::wstring::npos) {
            return result;
        }

        // ...and the RIGHTMOST `{{` strictly before it. That pairing is what
        // makes `{{{x}}}` parse as `{` + substitution + `}`.
        size_t open = std::wstring::npos;
        for (size_t j = close; j >= 1; --j) {
            if (result[j - 1] == L'{' && result[j] == L'{') {
                open = j - 1;
                break;
            }
            if (j == 1) {
                break;
            }
        }
        if (open == std::wstring::npos) {
            return std::nullopt;  // Unmatched `}}`.
        }

        std::wstring_view body(result.data() + open + 2, close - open - 2);
        std::wstring expanded;
        // A whole-value bare reference is the only form allowed to carry a
        // non-numeric captured value through verbatim, and the only one an
        // undefined variable must skip rather than blank out.
        if (std::wstring_view bare = BareIdentifier(body); !bare.empty()) {
            AddDependency(deps, bare);
            const StyleVariableValue* var = lookup ? lookup(bare) : nullptr;
            if (!var || !var->substitutable) {
                return std::nullopt;
            }
            expanded = var->text;
        } else {
            try {
                expanded = Evaluator(body, lookup, deps).Evaluate();
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }

        result.replace(open, close + 2 - open, expanded);
        scan_from = open + expanded.size();
    }
}

}  // namespace styler
```

- [ ] **Step 3: `PrepareTheme` para de pular os dinâmicos**

Em `src/core/include/styler/matcher.h`, `PreparedStyle` ganha:

```cpp
    // True when `value` still contains `{{ ... }}`. The value cannot be
    // resolved at preparation time - it depends on live captured properties -
    // so the engine expands it per element and re-expands it whenever a
    // variable it depends on changes.
    bool dynamic = false;
```

Em `src/core/matcher.cpp`, o bloco que hoje incrementa `skipped_dynamic` e faz `continue` vira:

```cpp
            p.value = ApplyStyleConstants(v.value, constants);
            if (p.value.find(L"{{") != std::wstring::npos) {
                // Left for the engine: the value depends on live captured
                // properties, so it is expanded per element (Task 6) and
                // re-expanded on every change. Constants are substituted
                // FIRST (found in the Plano 3 review): Pills hides
                // `{{__unset}}` inside a $constant, and checking the raw text
                // would miss it.
                p.dynamic = true;
                ++out.dynamic_values;
            }
```

`ResolvedTheme::skipped_dynamic` vira `ResolvedTheme::dynamic_values` (mesmo tipo, sentido invertido: agora conta o que **vai** ser resolvido). Ajustar a linha de log de `theme_session.cpp` para `%d dynamic values`.

`ResolvedTheme::skipped_captures` **continua existindo e continua sendo incrementado nesta task** — as capturas só são ligadas na Task 6, e o plano não deixa um estado intermediário em que o consumidor existe mas o produtor não. O que muda aqui é só que a linha de log deixa de dizer "skipped" para os dinâmicos.

> **Consequência que o implementador precisa aceitar:** entre esta task e a Task 6, um estilo dinâmico tem `dynamic = true` e **ainda não é aplicado** (a Task 6 adiciona o ramo que o expande). O comportamento visível não regride: hoje ele também não é aplicado. A Task 6 é o que o liga.

Um estilo `dynamic` e `is_xaml` ao mesmo tempo **não** pode ser pré-resolvido em `ResolveSetter`: o `ResolvedSetter` cacheado por `PreparedStyle*` seria o valor errado. A Task 6 trata disso; aqui basta garantir que `p.blur` nunca é preenchido para um estilo `dynamic` (um `<WindhawkBlur>` com `{{}}` dentro não existe no corpus e falharia no parser de qualquer forma).

- [ ] **Step 4: `tests/core/test_style_expression.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <styler/style_expression.h>

namespace {

// A lookup over a fixed table, with the values the shipped themes actually
// capture (ActualWidth / ActualHeight / Height - all numeric).
struct Vars {
    std::map<std::wstring, styler::StyleVariableValue> table;

    void SetNumber(const wchar_t* name, double v) {
        table[name] = styler::StyleVariableValue{
            styler::FormatDoubleInvariant(v), v, true};
    }
    void SetOpaque(const wchar_t* name, const wchar_t* class_name) {
        // A brush or a thickness: present, but not substitutable.
        table[name] = styler::StyleVariableValue{class_name, std::nullopt, false};
    }
    styler::StyleVariableLookup Lookup() const {
        return [this](std::wstring_view n) -> const styler::StyleVariableValue* {
            auto it = table.find(std::wstring(n));
            return it == table.end() ? nullptr : &it->second;
        };
    }
};

std::optional<std::wstring> Expand(const Vars& vars, const wchar_t* text) {
    std::vector<std::wstring> deps;
    return styler::ExpandStyleVariables(text, vars.Lookup(), &deps);
}

Vars CorpusVars() {
    Vars v;
    v.SetNumber(L"TaskbarHeight", 48);
    v.SetNumber(L"containerGridWidth", 1000);
    v.SetNumber(L"TaskHeight", 40);
    v.SetNumber(L"OverflowHeight", 300);
    v.SetNumber(L"BtnW", 44);
    v.SetNumber(L"ImageIconWidth", 24);
    v.SetNumber(L"LabelWidth", 0);
    v.SetNumber(L"WeatherTempWidth", 30);
    v.SetNumber(L"WeatherCondWidth", 60);
    v.SetNumber(L"WeatherIconWidth", 20);
    return v;
}

}  // namespace

TEST_CASE("text with no substitution passes through") {
    Vars v;
    CHECK(*Expand(v, L"plain text") == L"plain text");
    CHECK(*Expand(v, L"0,0,0,0") == L"0,0,0,0");
}

TEST_CASE("the expressions the shipped themes actually use") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{TaskbarHeight-(4+6)}}") == L"38");
    CHECK(*Expand(v, L"{{containerGridWidth>0?containerGridWidth:`Infinity`}}") ==
          L"1000");
    CHECK(*Expand(v,
                  L"{{containerGridWidth>0?max(containerGridWidth-250,100):"
                  L"`Infinity`}}") == L"750");
    CHECK(*Expand(v, L"{{(TaskHeight/4)*1.8}}") == L"18");
    CHECK(*Expand(v, L"{{ max(-4, min(-15, OverflowHeight * 0.35)) }}") == L"-4");
    CHECK(*Expand(v, L"{{BtnW-6}}") == L"38");
    CHECK(*Expand(v, L"{{max((ImageIconWidth/2-3),10)}}") == L"10");
    CHECK(*Expand(v, L"{{LabelWidth>0?6:0}}") == L"0");
    CHECK(*Expand(v, L"{{WeatherCondWidth+WeatherTempWidth + WeatherIconWidth + 36}}") ==
          L"146");
    CHECK(*Expand(v, L"{{-ImageIconWidth/2}}") == L"-12");
}

TEST_CASE("substitutions mix with literal text, several per value") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"0,0,0,{{TaskHeight - 8}}") == L"0,0,0,32");
    CHECK(*Expand(v, L"{{BtnW}},{{TaskHeight}},{{BtnW}},{{TaskHeight}}") ==
          L"44,40,44,40");
}

TEST_CASE("brace pairs match innermost first") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{{TaskHeight}}}") == L"{40}");
}

TEST_CASE("an undefined bare reference skips the style") {
    Vars v = CorpusVars();
    CHECK_FALSE(Expand(v, L"{{nope}}").has_value());
    // Pills' sentinel, which reaches here through a $constant.
    CHECK_FALSE(Expand(v, L"{{__unset}}").has_value());
}

TEST_CASE("an undefined variable inside an expression is the empty string") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{nope == `` ? 80 : nope}}") == L"80");
    // ...but arithmetic on it still fails, so the style is skipped rather
    // than treated as zero.
    CHECK_FALSE(Expand(v, L"{{nope + 1}}").has_value());
}

TEST_CASE("an opaque capture is not substitutable") {
    Vars v = CorpusVars();
    v.SetOpaque(L"Brushy", L"Windows.UI.Xaml.Media.SolidColorBrush");
    CHECK_FALSE(Expand(v, L"{{Brushy}}").has_value());
    // But it can still be compared, which is how a theme can branch on it.
    CHECK(*Expand(v, L"{{Brushy == `Windows.UI.Xaml.Media.SolidColorBrush` ? 1 : 0}}") ==
          L"1");
}

TEST_CASE("strings, comparisons and the conditional") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{TaskHeight == 40 ? `Auto` : `*`}}") == L"Auto");
    CHECK(*Expand(v, L"{{TaskHeight != 40 ? `Auto` : `*`}}") == L"*");
    CHECK(*Expand(v, L"{{TaskHeight >= 40}}") == L"1");
    CHECK(*Expand(v, L"{{TaskHeight < 40}}") == L"0");
    CHECK(*Expand(v, L"{{`a``b`}}") == L"a`b");
}

TEST_CASE("a malformed expression skips the style, it never throws") {
    Vars v = CorpusVars();
    CHECK_FALSE(Expand(v, L"{{1/0}}").has_value());
    CHECK_FALSE(Expand(v, L"{{TaskHeight +}}").has_value());
    CHECK_FALSE(Expand(v, L"a}}b").has_value());
    CHECK_FALSE(Expand(v, L"{{min(1)}}").has_value());
    CHECK_FALSE(Expand(v, L"{{nosuchfn(1,2)}}").has_value());
}

TEST_CASE("every referenced name is reported once, in use order") {
    Vars v = CorpusVars();
    std::vector<std::wstring> deps;
    styler::ExpandStyleVariables(L"{{BtnW - ImageIconWidth + nope}}", v.Lookup(),
                                 &deps);
    REQUIRE(deps.size() == 3);
    CHECK(deps[0] == L"BtnW");
    CHECK(deps[1] == L"ImageIconWidth");
    CHECK(deps[2] == L"nope");
}

TEST_CASE("FormatDoubleInvariant round-trips without trailing zeros") {
    CHECK(styler::FormatDoubleInvariant(38.0) == L"38");
    CHECK(styler::FormatDoubleInvariant(19.5) == L"19.5");
    CHECK(styler::FormatDoubleInvariant(-4.0) == L"-4");
}
```

- [ ] **Step 5: o corpus é o teste de verdade**

Acrescentar a `tests/core/test_corpus_resolution.cpp`:

```cpp
TEST_CASE("every dynamic value in the corpus parses with its variables bound") {
    namespace fs = std::filesystem;
    // Bind every name any theme captures to a plausible number, then expand
    // every dynamic value in the corpus. A value that comes back nullopt is
    // a grammar gap, not a theme bug: with all its variables defined and
    // numeric, every shipped expression must evaluate.
    std::map<std::wstring, styler::StyleVariableValue> table;
    std::vector<std::pair<std::wstring, std::wstring>> failures;

    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
            if (entry.path().extension() != ".json" ||
                entry.path().filename() == "credits.json") {
                continue;
            }
            styler::Theme theme = styler::LoadThemeFromFile(entry.path().wstring());
            for (const auto& rule : theme.rules) {
                for (const auto& style : rule.styles) {
                    if (const auto* c = std::get_if<styler::CaptureRule>(&style)) {
                        if (pass == 0) {
                            table[c->var_name] = styler::StyleVariableValue{
                                L"64", 64.0, true};
                        }
                        continue;
                    }
                }
            }
            if (pass == 0) {
                continue;
            }
            styler::ResolvedTheme resolved = styler::PrepareTheme(theme);
            auto lookup = [&table](std::wstring_view n)
                -> const styler::StyleVariableValue* {
                auto it = table.find(std::wstring(n));
                return it == table.end() ? nullptr : &it->second;
            };
            for (const auto& prepared_rule : resolved.rules) {
                for (const auto& style : prepared_rule.styles) {
                    if (!style.dynamic) {
                        continue;
                    }
                    std::vector<std::wstring> deps;
                    auto out = styler::ExpandStyleVariables(style.value, lookup,
                                                            &deps);
                    // Pills' `{{__unset}}` is a deliberate "leave this alone"
                    // sentinel and is the one expected skip.
                    if (!out && style.value.find(L"__unset") ==
                                    std::wstring::npos) {
                        failures.emplace_back(resolved.id, style.value);
                    }
                }
            }
        }
    }
    for (const auto& [theme_id, value] : failures) {
        MESSAGE("unevaluable dynamic value in ", theme_id);
    }
    CHECK(failures.empty());
}
```

- [ ] **Step 6: CMake**

`src/core/CMakeLists.txt`: `style_expression.cpp`. `tests/core/CMakeLists.txt`: `test_style_expression.cpp`.

**Test cycle:** build + `ctest --output-on-failure`. Esperado: `core` verde, com os 11 casos novos de `test_style_expression.cpp` e o caso novo de corpus. O smoke ao vivo não muda nada visualmente (a Task 6 é que liga); o que muda é a linha de log: `… 29 captures skipped, 168 dynamic values …` em vez de `… skipped`.

> **Se o caso de corpus falhar**, a mensagem nomeia o tema e o valor. Isso é exatamente o achado que este teste existe para produzir: ou a gramática tem um buraco, ou o tema usa algo que o upstream também não avalia. Registre o valor exato no ledger antes de mexer no avaliador.

**Commit:** `feat(core): avaliador de expressoes {{...}} de variaveis de estilo`

---

### Task 6: capturas e valores dinâmicos no TAP

A parte viva: `ActualWidth=>BtnW` passa a publicar um valor, `{{BtnW-6}}` passa a lê-lo, e uma mudança de layout reaplica quem depende dela. É a task com mais superfície de erro do plano, por isso vem depois do avaliador puro já testado.

**Files:**
- Create: `src/tap/style_variables.h`, `src/tap/style_variables.cpp`
- Modify: `src/core/include/styler/matcher.h`, `src/core/matcher.cpp`, `src/tap/style_engine.h`, `src/tap/style_engine.cpp`, `src/tap/property_setter.h`, `src/tap/property_setter.cpp`, `src/tap/theme_session.cpp`, `src/tap/CMakeLists.txt`, `docs/smoke-test.md`

**Interfaces:**
- Consumes: `ExpandStyleVariables`, `StyleVariableValue`, `FormatDoubleInvariant` (Task 5); `ElementId`, `ResolveSetter`, `SetCustom`, `ApplyBucketForState` (Plano 3).
- Produces:
  - `PreparedRule::captures` (`std::vector<PreparedCapture>`), `struct styler::PreparedCapture { std::wstring property; std::wstring var_name; }`.
  - Todo o `src/tap/style_variables.h` listado no Step 2.
  - `wux::DependencyProperty styler::tap::ResolveProperty(std::wstring_view type, std::wstring_view fallback_type, std::wstring_view property)` — o DP sem precisar do valor.
  - `void styler::tap::ReapplyDynamicProperty(ElementId, wux::DependencyProperty const&)` — o callback que `style_variables.cpp` chama.

- [ ] **Step 1: o core entrega as capturas**

`src/core/include/styler/matcher.h`:

```cpp
// `Prop=>Var` on a matched element: publish the element's live value of
// `property` as the style variable `var_name`.
struct PreparedCapture {
    std::wstring property;
    std::wstring var_name;
};
```

e `PreparedRule` ganha `std::vector<PreparedCapture> captures;`.

Em `src/core/matcher.cpp`, o ramo que hoje incrementa `skipped_captures` vira:

```cpp
            if (const auto* capture = std::get_if<CaptureRule>(&style)) {
                rule.captures.push_back(
                    PreparedCapture{capture->property_name, capture->var_name});
                continue;
            }
```

`ResolvedTheme::skipped_captures` vira `ResolvedTheme::captures` (contagem do que **vai** ser ligado), e o `if (rule.styles.empty()) continue;` vira `if (rule.styles.empty() && rule.captures.empty()) continue;` — uma regra que **só** captura precisa sobreviver, e hoje não sobrevive. Ajustar a linha de log de `theme_session.cpp` para `%d captures, %d dynamic values`.

Acrescentar a `tests/core/test_matcher.cpp`:

```cpp
TEST_CASE("PrepareTheme keeps capture rules") {
    styler::Theme theme;
    theme.id = L"T";
    styler::ThemeRule rule;
    rule.target = L"Grid";
    rule.selector = styler::ParseSelector(L"Grid");
    rule.styles.push_back(styler::ParseStyleRule(L"ActualWidth=>W"));
    theme.rules.push_back(std::move(rule));

    styler::ResolvedTheme resolved = styler::PrepareTheme(theme);
    REQUIRE(resolved.rules.size() == 1);
    REQUIRE(resolved.rules[0].captures.size() == 1);
    CHECK(resolved.rules[0].captures[0].property == L"ActualWidth");
    CHECK(resolved.rules[0].captures[0].var_name == L"W");
    CHECK(resolved.rules[0].styles.empty());
    CHECK(resolved.captures == 1);
}
```

- [ ] **Step 2: `src/tap/style_variables.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <styler/style_expression.h>
#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The identity of one live element's position in the visual tree: every
// ancestor's raw pointer, root first, the element itself last. Used only to
// score "which capturing element is closest to this consumer", by comparing
// how long a common prefix two chains share - upstream keeps a cached node
// graph for the same answer (ElementTreeLcaDepth, vendor:11728-11750). The
// pointers are never dereferenced; they are identity tokens, and a chain is
// recomputed on every capture change rather than cached, so a reparented
// element cannot leave a stale spine behind.
std::vector<void*> AncestorChain(wux::FrameworkElement const& element);

// Registers `element` as the source of `name`, reading `property`. Seeds the
// variable with the property's current value and subscribes to changes:
// SizeChanged for the layout-driven ActualWidth / ActualHeight (which never
// raise a property-changed callback, vendor:17199-17206), the property
// callback for everything else. Re-registering the same (element, property)
// is a no-op that logs.
void RegisterCapture(ElementId id, wux::FrameworkElement const& element,
                     wux::DependencyProperty const& property,
                     const std::wstring& name);

// Records that (`id`, `property`) used `deps` the last time its value was
// expanded, replacing whatever it used before. An empty `deps` unregisters it.
void RegisterConsumer(ElementId id, wux::DependencyProperty const& property,
                      const std::vector<std::wstring>& deps);

// The value of `name` as seen from a consumer whose ancestor chain is
// `consumer_chain`: of all the elements currently capturing `name`, the one
// sharing the longest ancestor prefix with the consumer wins; between equally
// close ones, the most recently registered. Null when nothing captures it.
const styler::StyleVariableValue* LookupForConsumer(
    std::wstring_view name, const std::vector<void*>& consumer_chain);

// A lookup bound to one consumer's chain, ready for ExpandStyleVariables.
styler::StyleVariableLookup LookupFor(const std::vector<void*>& consumer_chain);

// Drops every capture and every consumer registration this element owns, and
// propagates the loss of any variable it was the last source of.
void ForgetElementVariables(ElementId id);

// Drops everything on this thread. Called when the theme changes or the
// session ends, before the engine restores elements.
void ClearStyleVariablesOnThisThread();

// Installed once by the style engine: re-expands and re-applies one property
// of one element. style_variables.cpp calls it for every consumer of a
// variable whose value changed; keeping it a callback is what stops
// style_variables.cpp and style_engine.cpp from including each other.
using ReapplyPropertyFn =
    std::function<void(ElementId, wux::DependencyProperty const&)>;
void SetReapplyPropertyCallback(ReapplyPropertyFn fn);

// How many variables are currently defined on this thread, for the log.
size_t DefinedVariableCount();

}  // namespace styler::tap
```

- [ ] **Step 3: `src/tap/style_variables.cpp`**

Este arquivo foi **compilado** ao escrever o plano, com os headers reais do projeto (`cl /std:c++20 /EHsc /permissive- /W4 /utf-8 /DWINRT_LEAN_AND_MEAN /bigobj`): 0 erros, 0 warnings.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_variables.h>

#include <algorithm>
#include <map>
#include <unordered_map>

#include <tap/log.h>

namespace styler::tap {
namespace {

// One element capturing one variable.
struct CaptureSite {
    ElementId id = ElementId::None;
    winrt::weak_ref<wux::FrameworkElement> element;
    std::vector<void*> chain;  // Root first, the capturing element last.
    styler::StyleVariableValue value;
    uint64_t sequence = 0;  // Registration order, for the closest-tie rule.
};

// One property of one element whose value text references variables.
struct ConsumerSite {
    ElementId id = ElementId::None;
    wux::DependencyProperty property{nullptr};
};

struct CaptureSubscription {
    wux::DependencyProperty property{nullptr};
    std::wstring name;
    long long property_changed_token = 0;  // 0 when SizeChanged covers it.
};

struct ElementCaptures {
    std::vector<CaptureSubscription> subscriptions;
    winrt::event_token size_changed_token{};
    winrt::weak_ref<wux::FrameworkElement> element;
};

// All of this is per XAML host thread, like every other piece of state that
// points at elements (spec section 6.1). NOT keyed by XamlRoot the way
// upstream is (vendor:12054-12100): each taskbar surface already runs on its
// own thread here, and no shipped theme captures the same variable name twice
// (measured over all 55). The cost if that changes: two XamlRoots sharing one
// thread would share a variable of the same name, which reads one surface's
// number on the other - one wrong number, never a crash, and the fix is one
// more map level.
thread_local std::unordered_map<std::wstring, std::vector<CaptureSite>>
    t_variables;
thread_local std::unordered_map<std::wstring, std::vector<ConsumerSite>>
    t_consumers;
thread_local std::unordered_map<ElementId, ElementCaptures> t_element_captures;
// Every variable name each (element, property) consumer last depended on, so
// a re-expansion can retract the ones it stopped using.
thread_local std::map<std::pair<ElementId, void*>, std::vector<std::wstring>>
    t_consumer_deps;
thread_local uint64_t t_sequence = 0;
// Guards against a propagation that re-enters itself: re-applying a property
// can change a captured layout property, which propagates again. Upstream
// caps the same way (vendor:12045-12052).
thread_local int t_propagation_depth = 0;
constexpr int kMaxPropagationDepth = 8;

ReapplyPropertyFn& ReapplyCallback() {
    static ReapplyPropertyFn fn;
    return fn;
}

bool IsLayoutDrivenProperty(wux::DependencyProperty const& property) {
    return property == wux::FrameworkElement::ActualWidthProperty() ||
           property == wux::FrameworkElement::ActualHeightProperty();
}

// The effective value, not the local one: ActualWidth never has a local value,
// so ReadLocalValue would report Unset for exactly the properties the shipped
// themes capture (vendor:16775-16781).
styler::StyleVariableValue ReadCapturedValue(
    wux::FrameworkElement const& element,
    wux::DependencyProperty const& property) {
    styler::StyleVariableValue out;
    wf::IInspectable value{nullptr};
    try {
        value = element.GetValue(property);
    } catch (winrt::hresult_error const&) {
        return out;
    } catch (...) {
        return out;
    }
    if (!value || value == wux::DependencyProperty::UnsetValue()) {
        return out;
    }
    try {
        if (auto boxed = value.try_as<wf::IPropertyValue>()) {
            switch (boxed.Type()) {
                case wf::PropertyType::Double:
                    out.number = boxed.GetDouble();
                    break;
                case wf::PropertyType::Single:
                    out.number = boxed.GetSingle();
                    break;
                case wf::PropertyType::Int32:
                    out.number = boxed.GetInt32();
                    break;
                case wf::PropertyType::UInt32:
                    out.number = boxed.GetUInt32();
                    break;
                case wf::PropertyType::Int64:
                    out.number = static_cast<double>(boxed.GetInt64());
                    break;
                case wf::PropertyType::Boolean:
                    out.text = boxed.GetBoolean() ? L"True" : L"False";
                    out.substitutable = true;
                    return out;
                case wf::PropertyType::String:
                    out.text = boxed.GetString();
                    out.substitutable = true;
                    return out;
                default:
                    break;
            }
            if (out.number) {
                out.text = styler::FormatDoubleInvariant(*out.number);
                out.substitutable = true;
                return out;
            }
        }
        // A brush, a thickness, an enum box we do not understand: record the
        // class name so `{{Var == `...`}}` can still branch on it, but leave
        // `substitutable` false so a bare `{{Var}}` skips the style instead of
        // writing a class name into the XAML.
        out.text = winrt::get_class_name(value);
    } catch (winrt::hresult_error const&) {
        out.text.clear();
    } catch (...) {
        out.text.clear();
    }
    return out;
}

bool SameValue(const styler::StyleVariableValue& a,
               const styler::StyleVariableValue& b) {
    return a.substitutable == b.substitutable && a.number == b.number &&
           a.text == b.text;
}

void PropagateChange(const std::wstring& name) {
    if (t_propagation_depth >= kMaxPropagationDepth) {
        STYLER_LOG(LogLevel::Error, L"variable %s: propagation depth capped",
                   name.c_str());
        return;
    }
    auto it = t_consumers.find(name);
    if (it == t_consumers.end() || !ReapplyCallback()) {
        return;
    }
    // A copy: re-applying a property can register or drop consumers, and
    // upstream hit exactly this (a nested propagation invalidating the list
    // being walked).
    std::vector<ConsumerSite> sites = it->second;
    ++t_propagation_depth;
    for (const ConsumerSite& site : sites) {
        try {
            ReapplyCallback()(site.id, site.property);
        } catch (winrt::hresult_error const&) {
        } catch (...) {
        }
    }
    --t_propagation_depth;
}

void SetCaptureValue(const std::wstring& name, ElementId id,
                     styler::StyleVariableValue value) {
    auto it = t_variables.find(name);
    if (it == t_variables.end()) {
        return;
    }
    for (CaptureSite& site : it->second) {
        if (site.id != id) {
            continue;
        }
        if (SameValue(site.value, value)) {
            return;  // Nothing to propagate.
        }
        site.value = std::move(value);
        // The chain can have changed since registration (a reparent), and it
        // is what decides which consumer reads this capture.
        if (auto element = site.element.get()) {
            site.chain = AncestorChain(element);
        }
        PropagateChange(name);
        return;
    }
}

}  // namespace

std::vector<void*> AncestorChain(wux::FrameworkElement const& element) {
    std::vector<void*> chain;
    try {
        wux::DependencyObject node = element;
        // 64 is well past any real taskbar depth and bounds a tree that a
        // future Windows build might make pathological.
        for (int depth = 0; node && depth < 64; ++depth) {
            chain.push_back(winrt::get_abi(node));
            node = wuxm::VisualTreeHelper::GetParent(node);
        }
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
    std::reverse(chain.begin(), chain.end());  // Root first.
    return chain;
}

void RegisterCapture(ElementId id, wux::FrameworkElement const& element,
                     wux::DependencyProperty const& property,
                     const std::wstring& name) {
    ElementCaptures& captures = t_element_captures[id];
    captures.element = element;
    for (const CaptureSubscription& existing : captures.subscriptions) {
        if (existing.property == property) {
            STYLER_LOG(LogLevel::Error,
                       L"capture: this element already captures that property "
                       L"as '%s'; dropping '%s'",
                       existing.name.c_str(), name.c_str());
            return;
        }
    }

    CaptureSite site;
    site.id = id;
    site.element = element;
    site.chain = AncestorChain(element);
    site.value = ReadCapturedValue(element, property);
    site.sequence = ++t_sequence;

    std::vector<CaptureSite>& sites = t_variables[name];
    std::erase_if(sites, [id](const CaptureSite& s) { return s.id == id; });
    sites.push_back(std::move(site));

    CaptureSubscription subscription;
    subscription.property = property;
    subscription.name = name;

    if (IsLayoutDrivenProperty(property)) {
        // ActualWidth / ActualHeight never raise a property-changed callback;
        // SizeChanged is the notification for them. One subscription per
        // element covers every layout-driven capture it has.
        if (!captures.size_changed_token) {
            winrt::weak_ref<wux::FrameworkElement> weak = element;
            captures.size_changed_token = element.SizeChanged(
                [id, weak](wf::IInspectable const&,
                           wux::SizeChangedEventArgs const&) {
                    try {
                        auto live = weak.get();
                        if (!live) {
                            return;
                        }
                        auto it = t_element_captures.find(id);
                        if (it == t_element_captures.end()) {
                            return;
                        }
                        // A copy: SetCaptureValue propagates, which can touch
                        // t_element_captures.
                        std::vector<CaptureSubscription> subs =
                            it->second.subscriptions;
                        for (const CaptureSubscription& sub : subs) {
                            if (!IsLayoutDrivenProperty(sub.property)) {
                                continue;
                            }
                            SetCaptureValue(sub.name, id,
                                            ReadCapturedValue(live, sub.property));
                        }
                    } catch (winrt::hresult_error const&) {
                    } catch (...) {
                    }
                });
        }
    } else {
        winrt::weak_ref<wux::FrameworkElement> weak = element;
        std::wstring captured_name = name;
        subscription.property_changed_token =
            element.RegisterPropertyChangedCallback(
                property, [id, weak, captured_name](
                              wux::DependencyObject const&,
                              wux::DependencyProperty const& changed) {
                    try {
                        auto live = weak.get();
                        if (!live) {
                            return;
                        }
                        SetCaptureValue(captured_name, id,
                                        ReadCapturedValue(live, changed));
                    } catch (winrt::hresult_error const&) {
                    } catch (...) {
                    }
                });
    }

    captures.subscriptions.push_back(std::move(subscription));

    // A new capture can be closer to consumers that registered before this
    // element was ever matched, so they have to be re-evaluated even though
    // the variable itself is not "new".
    PropagateChange(name);
}

void RegisterConsumer(ElementId id, wux::DependencyProperty const& property,
                      const std::vector<std::wstring>& deps) {
    auto key = std::make_pair(id, winrt::get_abi(property));
    auto it = t_consumer_deps.find(key);
    if (it != t_consumer_deps.end()) {
        for (const std::wstring& old : it->second) {
            auto cit = t_consumers.find(old);
            if (cit == t_consumers.end()) {
                continue;
            }
            std::erase_if(cit->second, [&](const ConsumerSite& s) {
                return s.id == id && s.property == property;
            });
            if (cit->second.empty()) {
                t_consumers.erase(cit);
            }
        }
        t_consumer_deps.erase(it);
    }
    if (deps.empty()) {
        return;
    }
    for (const std::wstring& name : deps) {
        t_consumers[name].push_back(ConsumerSite{id, property});
    }
    t_consumer_deps.emplace(key, deps);
}

const styler::StyleVariableValue* LookupForConsumer(
    std::wstring_view name, const std::vector<void*>& consumer_chain) {
    auto it = t_variables.find(std::wstring(name));
    if (it == t_variables.end() || it->second.empty()) {
        return nullptr;
    }
    const CaptureSite* best = nullptr;
    size_t best_depth = 0;
    for (const CaptureSite& site : it->second) {
        size_t depth = 0;
        while (depth < site.chain.size() && depth < consumer_chain.size() &&
               site.chain[depth] == consumer_chain[depth]) {
            ++depth;
        }
        // Strictly deeper wins; equally deep, the later registration wins -
        // the documented tie-break (vendor:363-367).
        if (!best || depth > best_depth ||
            (depth == best_depth && site.sequence > best->sequence)) {
            best = &site;
            best_depth = depth;
        }
    }
    return best ? &best->value : nullptr;
}

styler::StyleVariableLookup LookupFor(
    const std::vector<void*>& consumer_chain) {
    return [&consumer_chain](std::wstring_view name)
               -> const styler::StyleVariableValue* {
        return LookupForConsumer(name, consumer_chain);
    };
}

void ForgetElementVariables(ElementId id) {
    auto it = t_element_captures.find(id);
    if (it != t_element_captures.end()) {
        auto element = it->second.element.get();
        if (element) {
            try {
                if (it->second.size_changed_token) {
                    element.SizeChanged(it->second.size_changed_token);
                }
                for (const CaptureSubscription& sub : it->second.subscriptions) {
                    if (sub.property_changed_token) {
                        element.UnregisterPropertyChangedCallback(
                            sub.property, sub.property_changed_token);
                    }
                }
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        }
        // Collected before erasing, so the propagation below sees the state
        // WITHOUT this element's captures.
        std::vector<std::wstring> names;
        for (const CaptureSubscription& sub : it->second.subscriptions) {
            names.push_back(sub.name);
        }
        t_element_captures.erase(it);
        for (const std::wstring& name : names) {
            auto vit = t_variables.find(name);
            if (vit == t_variables.end()) {
                continue;
            }
            std::erase_if(vit->second,
                          [id](const CaptureSite& s) { return s.id == id; });
            if (vit->second.empty()) {
                t_variables.erase(vit);
            }
            PropagateChange(name);
        }
    }

    // Consumer registrations keyed by this element, whatever the property.
    std::vector<std::pair<ElementId, void*>> keys;
    for (const auto& [key, deps] : t_consumer_deps) {
        if (key.first == id) {
            keys.push_back(key);
        }
    }
    for (const auto& key : keys) {
        auto dit = t_consumer_deps.find(key);
        if (dit == t_consumer_deps.end()) {
            continue;
        }
        for (const std::wstring& name : dit->second) {
            auto cit = t_consumers.find(name);
            if (cit == t_consumers.end()) {
                continue;
            }
            std::erase_if(cit->second, [id](const ConsumerSite& s) {
                return s.id == id;
            });
            if (cit->second.empty()) {
                t_consumers.erase(cit);
            }
        }
        t_consumer_deps.erase(dit);
    }
}

void ClearStyleVariablesOnThisThread() {
    std::vector<ElementId> ids;
    ids.reserve(t_element_captures.size());
    for (const auto& [id, captures] : t_element_captures) {
        ids.push_back(id);
    }
    for (ElementId id : ids) {
        ForgetElementVariables(id);
    }
    t_variables.clear();
    t_consumers.clear();
    t_consumer_deps.clear();
    t_element_captures.clear();
}

void SetReapplyPropertyCallback(ReapplyPropertyFn fn) {
    ReapplyCallback() = std::move(fn);
}

size_t DefinedVariableCount() {
    return t_variables.size();
}

}  // namespace styler::tap
```

- [ ] **Step 4: `ResolveProperty` — o DP sem o valor**

Um estilo dinâmico precisa do `DependencyProperty` **antes** de saber o valor: é ele que decide quem reivindica a propriedade e é ele a chave do consumidor. `src/tap/property_setter.h`:

```cpp
// The DependencyProperty a name resolves to, without needing a value - the
// dynamic path, where the value is only known per element and can change
// afterwards. Implemented with the same <Style><Setter> trick ResolveSetter
// uses, with an empty value. Throws winrt::hresult_error when the name does
// not resolve.
wux::DependencyProperty ResolveProperty(std::wstring_view type,
                                        std::wstring_view fallback_type,
                                        std::wstring_view property);
```

`src/tap/property_setter.cpp`:

```cpp
wux::DependencyProperty ResolveProperty(std::wstring_view type,
                                        std::wstring_view fallback_type,
                                        std::wstring_view property) {
    styler::PreparedStyle probe;
    probe.property = std::wstring(property);
    probe.is_xaml = true;  // An empty XAML value is the "clear" form, which
                           // parses without needing to know the value type.
    return ResolveSetter(type, fallback_type, probe).property;
}
```

- [ ] **Step 5: o motor expande por elemento**

Em `src/tap/style_engine.cpp`:

`ElementState` ganha o que a reexpansão precisa saber sem reencontrar o elemento:

```cpp
struct ElementState {
    winrt::weak_ref<wux::FrameworkElement> element;
    std::vector<VsgBucket> buckets;
    // The two type names ResolveSetter needs, captured once at match time:
    // re-expanding a dynamic value later must resolve it against the same
    // type it first resolved against.
    std::wstring type;
    std::wstring reported;
};
```

`PropertyState` ganha os estilos dinâmicos, por estado visual:

```cpp
    // For a dynamic value (`{{...}}`): the style whose text is re-expanded
    // per element, keyed by visual state name exactly like `values`. Empty
    // for a static property. Points into the ResolvedTheme, which t_cache_theme
    // keeps alive.
    std::map<std::wstring, const styler::PreparedStyle*> dynamic_styles;
```

Uma função nova, no namespace anônimo, com a expansão de um estilo:

```cpp
// Expands one dynamic style's text against the variables visible from
// `chain`, resolves the result to a value, and records what it depended on.
// Returns false when the style must be skipped this time round (an undefined
// variable, a bad expression) - the dependencies are STILL registered, so the
// style comes back on its own once something captures what it needs.
bool ResolveDynamicValue(ElementId id, wux::DependencyProperty const& property,
                         const styler::PreparedStyle& style,
                         const std::vector<void*>& chain,
                         std::wstring_view type, std::wstring_view reported,
                         wf::IInspectable* out) {
    std::vector<std::wstring> deps;
    std::optional<std::wstring> text =
        styler::ExpandStyleVariables(style.value, LookupFor(chain), &deps);
    RegisterConsumer(id, property, deps);
    if (!text) {
        STYLER_LOG(LogLevel::Debug, L"dynamic %s on %s: unresolved for now",
                   style.property.c_str(), std::wstring(type).c_str());
        return false;
    }
    styler::PreparedStyle expanded = style;
    expanded.value = std::move(*text);
    expanded.dynamic = false;
    ResolvedSetter setter = ResolveSetter(type, reported, expanded);
    *out = setter.clear ? nullptr : setter.value;
    return true;
}
```

No laço de estilos de `OnElementAdded`, o bloco que hoje é

```cpp
                const ResolvedSetter* setter = CachedSetter(theme, style, type, reported);
```

passa a tratar os dois casos. O corpo inteiro do `try` fica:

```cpp
                wux::DependencyProperty property{nullptr};
                wf::IInspectable value;
                bool have_value = false;
                const styler::BlurSpec* blur = nullptr;

                if (style.dynamic) {
                    // Never cached: the text differs per element and changes
                    // afterwards. The property still resolves the same way,
                    // and must resolve even when the value does not, or the
                    // consumer could not be registered and the style would
                    // never come back.
                    property = ResolveProperty(type, reported, style.property);
                    have_value = ResolveDynamicValue(id, property, style, chain,
                                                     type, reported, &value);
                } else {
                    const ResolvedSetter* setter =
                        CachedSetter(theme, style, type, reported);
                    property = setter->property;
                    value = setter->clear ? nullptr : setter->value;
                    blur = setter->blur;
                    have_value = true;
                }

                if (claimed.contains(property)) {
                    continue;  // An earlier (later-in-theme) match owns it.
                }
                claimed_by_match.insert(property);
                if (!style.visual_state.empty() && !group) {
                    STYLER_LOG(LogLevel::Debug, L"rule %zu: %s@%s without a group, inert",
                               match.rule->source_index, style.property.c_str(),
                               style.visual_state.c_str());
                    continue;
                }
                PropertyState& prop = bucket.properties[property];
                if (style.dynamic) {
                    prop.dynamic_styles[style.visual_state] = &style;
                }
                if (blur) {
                    if (auto brush = MakeBlurBrush(element, *blur)) {
                        value = brush;
                        ++t_stats.blur_brushes;
                    } else {
                        ++t_stats.blur_fallbacks;
                        STYLER_LOG(LogLevel::Error,
                                   L"blur fell back to AcrylicBrush on %s",
                                   type.c_str());
                    }
                }
                if (have_value) {
                    prop.values[style.visual_state] = value;
                }
```

`chain` é calculado uma vez por elemento, logo depois de `state.element = element;`:

```cpp
    state.type = type;
    state.reported = reported;
    // Computed once per report: every dynamic style on this element resolves
    // its variables from the same position in the tree.
    const std::vector<void*> chain = AncestorChain(element);
```

(`type` já é calculado nessa altura; mover a linha `std::wstring type = view.TypeName();` para antes deste bloco se ainda não estiver.)

As capturas são registradas **depois** do laço de estilos de cada match, junto com o `claimed.insert`:

```cpp
        for (const styler::PreparedCapture& capture : match.rule->captures) {
            try {
                RegisterCapture(id, element,
                                ResolveProperty(type, reported, capture.property),
                                capture.var_name);
            } catch (winrt::hresult_error const& ex) {
                ++t_stats.failed_styles;
                STYLER_LOG(LogLevel::Error, L"capture %s=>%s on %s: 0x%08X",
                           capture.property.c_str(), capture.var_name.c_str(),
                           type.c_str(), static_cast<unsigned>(ex.code()));
            } catch (...) {
                ++t_stats.failed_styles;
            }
        }
```

> **Ordem importa:** as capturas de um elemento são registradas **depois** dos consumidores dele, e `RegisterCapture` termina com um `PropagateChange`. É isso que faz um elemento que captura *e* consome (o `Taskbar.TaskListButton` de `Pills` captura `BtnW` e consome `{{BtnW-6}}`) convergir na mesma passagem, sem precisar de uma segunda.

- [ ] **Step 6: reaplicar um consumidor**

Ainda em `src/tap/style_engine.cpp`, no namespace público:

```cpp
void ReapplyDynamicProperty(ElementId id, wux::DependencyProperty const& property) {
    auto it = t_state.find(id);
    if (it == t_state.end()) {
        return;
    }
    ElementState& state = it->second;
    auto element = state.element.get();
    if (!element) {
        return;
    }
    const std::vector<void*> chain = AncestorChain(element);
    for (size_t i = 0; i < state.buckets.size(); ++i) {
        VsgBucket& bucket = state.buckets[i];
        auto pit = bucket.properties.find(property);
        if (pit == bucket.properties.end() || pit->second.dynamic_styles.empty()) {
            continue;
        }
        PropertyState& prop = pit->second;
        for (const auto& [visual_state, style] : prop.dynamic_styles) {
            wf::IInspectable value;
            if (ResolveDynamicValue(id, property, *style, chain, state.type,
                                    state.reported, &value)) {
                prop.values[visual_state] = value;
            } else {
                // Unresolvable again: drop the entry so PickValue falls
                // through to Unapply rather than re-pushing a stale number.
                prop.values.erase(visual_state);
            }
        }
        ApplyBucketForState(id, element, bucket, CurrentStateName(bucket.group),
                            false);
        return;  // A property lives in exactly one bucket.
    }
}
```

e, em `src/tap/style_engine.h`:

```cpp
// Re-expands and re-applies one dynamic property of one element. Installed as
// style_variables.cpp's propagation callback; not called from anywhere else.
void ReapplyDynamicProperty(ElementId id, wux::DependencyProperty const& property);
```

`SetTheme` instala o callback e limpa o estado das variáveis:

```cpp
void SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme) {
    g_theme.store(std::move(theme));
}
```

não muda (é global); quem limpa por thread é `RestoreAllOnThisThread`, que ganha, **antes** do laço de restauração:

```cpp
    // Before restoring: a capture teardown propagates, and propagating while
    // the elements are being restored would re-apply values onto elements
    // this call is about to undo - the same ordering bug the Plano 3 final
    // review found in the reload path (B1).
    ClearStyleVariablesOnThisThread();
```

e `OnElementRemoved` ganha `ForgetElementVariables(id);` antes do `RestoreElement`.

O callback é instalado uma vez por thread, em `InitializeForCurrentThread` (`src/tap/thread_init.cpp`) — não, **em `OnElementAdded`, na primeira chamada da thread**, porque `thread_init.cpp` não pode incluir `style_engine.h` sem inverter a dependência. A forma mínima:

```cpp
    // Installed once per thread, on the first styled element: the callback is
    // a thread_local hop from the variable store back into the engine.
    static thread_local bool t_callback_installed = false;
    if (!t_callback_installed) {
        t_callback_installed = true;
        SetReapplyPropertyCallback(&ReapplyDynamicProperty);
    }
```

logo no começo de `OnElementAdded`, depois do teste de `theme`.

- [ ] **Step 7: CMake, log e docs**

`src/tap/CMakeLists.txt`: `style_variables.cpp` no alvo `taskbar_styler_tap`.

`release_queue.cpp`, na linha do primeiro dreno, acrescentar `%zu variables` com `DefinedVariableCount()`.

`docs/smoke-test.md`, novos itens:

```
13. **Variáveis de estilo.** `apply Pills`. No log: `7 captures, 81 dynamic
    values`. Visual: os botões de app viram pílulas e **cada** pílula tem a
    largura do próprio botão (prova da escolha do capturador mais próximo —
    se todos ficarem com a mesma largura, o escore está errado).
14. **Reação ao layout.** Com `Pills` aplicado, abra e feche aplicativos até a
    taskbar mudar de largura, e passe o mouse por um botão com rótulo. As
    pílulas reacompanham sem reaplicar o tema; em `Debug` o log mostra
    `dynamic ... unresolved for now` no máximo durante o primeiro relatório de
    cada botão, nunca em regime.
15. **Blob.** `apply Blob` — 3 capturas e 50 dinâmicos; os paddings do
    `{{$buttonSpacing-2}},{{$taskbarTopOffset}},…` saem simétricos.
```

**Test cycle:**

1. Build + `ctest` — `core` verde com o caso novo de `test_matcher.cpp`.
2. **Smoke ao vivo, obrigatório:**
   - `apply Pills` → log `theme Pills: 49 rules prepared, 7 captures, 81 dynamic values, 1 blur brushes, 0 blur fallbacks`, depois `initial apply: … N variables` com `N >= 1`.
   - Largura por botão conferida a olho (item 13 acima).
   - `apply Blob` e `apply LiquidGlass2` — sem `ERR`, sem crescimento de `held`.
   - `reset` → `restored N`, `held=0`, taskbar padrão.
   - **Estabilidade:** dez minutos com `Pills` aplicado e a taskbar em uso; `held` volta a 0 depois de cada dreno e a contagem de `ERR` fica em zero. Uma `SizeChanged` por frame que propagasse indefinidamente apareceria aqui como log crescendo sem parar — é o modo de falha que este item existe para pegar.
3. Se o log mostrar `propagation depth capped`, registre no ledger qual variável: significa um ciclo (uma propriedade dinâmica que altera o que ela mesma captura) que o cap está contendo, mas que precisa de uma decisão explícita.

**Commit:** `feat(tap): capturas Prop=>Var e valores dinamicos {{...}}`

---

### Task 7: reciclagem do `ItemsRepeater` — spike primeiro

O upstream trata a reciclagem porque o pool **recicla sem Remove/Add**: os estilos casados para o item A ficam no elemento quando ele volta como item B. Se isso acontece na taskbar real é uma pergunta de runtime, e este plano **não a responde estaticamente**. A task portanto começa por um spike, e pode terminar sem código.

> **Sem `Microsoft.UI.Xaml`.** O `muxc::ItemsRepeater` do upstream está fora (Global Constraints). O que está dentro é escutar, no próprio elemento de item, um sinal de `Windows.UI.Xaml` que a reciclagem produz: `FrameworkElement::DataContextChanged` (o item passa a representar outro dado) ou a troca de `UIElement::Visibility` (o pool **recolhe o elemento e o mantém parenteado** — `vendor:18114-18117`). O spike decide qual, medindo. Nenhum dos dois exige a projeção.

- [ ] **Step 0 (SPIKE, obrigatório antes de escrever qualquer código desta task)**

**Perguntas exatas que o spike responde:**

1. *A taskbar recicla?* Com `Pills` aplicado (tem estilos por botão de app, é onde o defeito seria visível), abra seis aplicativos, feche os três do meio, abra outros três. Em `Debug`, o log registra `styled Taskbar.TaskListButton#TaskListButton` por botão novo. **Se cada botão novo produzir um `styled`, não há reciclagem observável e a task inteira sai de escopo.** Se um botão aparecer sem `styled` correspondente e visualmente errado (a pílula de outro app, largura errada, rótulo com o padding do anterior), há o defeito.
2. *Qual sinal delata a reciclagem?* Instrumentação temporária em `OnElementAdded`, apenas para o spike, em elementos cujo tipo contenha `TaskListButton`:

```cpp
    // SPIKE ONLY - not part of the shipped change.
    element.DataContextChanged([](wux::FrameworkElement const& s,
                                  wux::DataContextChangedEventArgs const&) {
        STYLER_LOG(LogLevel::Info, L"spike: DataContextChanged on %s",
                   winrt::get_class_name(s).c_str());
    });
    element.RegisterPropertyChangedCallback(
        wux::UIElement::VisibilityProperty(),
        [](wux::DependencyObject const& s, wux::DependencyProperty const&) {
            auto e = s.try_as<wux::UIElement>();
            STYLER_LOG(LogLevel::Info, L"spike: Visibility -> %d on %s",
                       e ? static_cast<int>(e.Visibility()) : -1,
                       winrt::get_class_name(s).c_str());
        });
```

   Repita o roteiro do item 1 e anote: qual dos dois dispara, em que ordem, e se o `DataContext` já é o do **novo** item quando dispara (comparar com o rótulo visível do botão).

**Saídas possíveis, todas aceitáveis:**

- **Sem defeito observável** → a task termina aqui. Registrar no ledger o roteiro, o log e a conclusão, e remover o item correspondente do "Fora deste plano" do Plano 4. **Esta é a saída que o plano considera provável o bastante para não presumir o contrário.**
- **Defeito, e `DataContextChanged` dispara com o dado novo** → Step 1 com `DataContextChanged`.
- **Defeito, e só a `Visibility` delata** → Step 1 com `Visibility`, sabendo que `Visible` pode chegar antes do novo `DataContext`; nesse caso o Step 1 posta a reaplicação no dispatcher da thread (`DispatcherQueue::GetForCurrentThread().TryEnqueue`) em vez de fazê-la no próprio callback, para cair depois do bind.
- **Nenhum dos dois dispara, e o defeito existe** → a task é cortada e o ledger registra que a única via conhecida é a projeção `Microsoft.UI.Xaml`, que as Global Constraints proíbem. O Plano 4 decide se a restrição muda.

- [ ] **Step 1: reaplicar a subárvore do item reciclado** *(só se o spike mostrou o defeito)*

A lógica é a do upstream (`vendor:18074-18113`), com o gatilho trocado. Em `src/tap/style_engine.cpp`:

```cpp
// A virtualizing container recycles its item elements instead of destroying
// them, so XAML diagnostics reports no Remove/Add and the styles matched for
// the previous item stay on the element. Tear the whole subtree down and
// re-match it: a rule can match a descendant through a condition on an
// ancestor, and descendants of a reused element get no report of their own.
// `applying == false` is the teardown half, `true` the rebuild.
void ReapplyCustomizationsForSubtree(wux::FrameworkElement const& element,
                                     bool applying) {
    try {
        if (ElementId id = FindElementId(element); id != ElementId::None) {
            ElementState* state = nullptr;
            if (auto it = t_state.find(id); it != t_state.end()) {
                state = &it->second;
            }
            if (state) {
                RestoreElement(id, *state);
                t_state.erase(id);
            }
            ForgetElementVariables(id);
            if (applying) {
                OnElementAdded(id, element, nullptr);
            }
        }
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }

    // Snapshotted: applying a style runs arbitrary XAML work, which can
    // change the children collection mid-walk.
    std::vector<wux::FrameworkElement> children;
    try {
        int count = wuxm::VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0; i < count; ++i) {
            if (auto child = wuxm::VisualTreeHelper::GetChild(element, i)
                                 .try_as<wux::FrameworkElement>()) {
                children.push_back(std::move(child));
            }
        }
    } catch (winrt::hresult_error const&) {
        return;
    } catch (...) {
        return;
    }
    for (const auto& child : children) {
        ReapplyCustomizationsForSubtree(child, applying);
    }
}
```

`FindElementId` já existe em `src/tap/element_registry.h` (o Plano 3 o usa na fila de liberação); se a assinatura for por handle e não por elemento, acrescentar o overload que recebe o `FrameworkElement` e converte com `winrt::get_abi`.

O gatilho, registrado em `OnElementAdded` para todo elemento cujo id o motor passou a acompanhar — o spike diz qual dos dois blocos fica:

```cpp
    // Recycled-item detection. Registered on every styled element, not only
    // on repeater items: the engine has no way to ask "is my parent a
    // repeater" without the Microsoft.UI.Xaml projection, and a spurious
    // trigger only costs one restore + re-match, which is idempotent.
    state.recycle_revoker = element.DataContextChanged(
        winrt::auto_revoke,
        [id](wux::FrameworkElement const& sender,
             wux::DataContextChangedEventArgs const&) {
            try {
                STYLER_LOG(LogLevel::Debug, L"element %llu reused",
                           static_cast<unsigned long long>(id));
                ReapplyCustomizationsForSubtree(sender, true);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        });
```

com `ElementState` ganhando `wux::FrameworkElement::DataContextChanged_revoker recycle_revoker;` (ou o token de `Visibility`, na outra saída do spike).

> **Custo aceito, explicitamente:** este gatilho dispara também quando o próprio shell troca o `DataContext` de um elemento por motivo nenhum a ver com reciclagem. O efeito é um restore + re-match do mesmo elemento com o mesmo tema, que é idempotente por construção (o motor guarda `original` na primeira aplicação e o recoloca antes de reaplicar). O que **não** é idempotente é o custo: se o log mostrar o mesmo id sendo reusado dezenas de vezes por segundo, o gatilho está errado e a task volta ao spike.

**Test cycle:**

- Sem defeito: nenhum código, só o registro no ledger com o log do spike. Nada a compilar.
- Com defeito e correção: build + `ctest` (nada novo de unitário — isto é puramente runtime), e o roteiro do spike repetido com o log mostrando `element N reused` seguido de `styled Taskbar.TaskListButton#...` no mesmo instante, e a pílula visualmente correta no botão novo. `held` volta a 0 no dreno seguinte, e a contagem de `styled` não cresce sem limite ao abrir e fechar aplicativos por dois minutos.

**Commit:** `feat(tap): reaplica estilos em item reciclado do ItemsRepeater` *(ou `docs: reciclagem do ItemsRepeater medida como fora de escopo`, conforme a saída do spike)*

---

## Auto-revisão

**O plano fecha o escopo pedido?**

| Item pedido | Onde | Estado |
|---|---|---|
| `WindhawkBlur` real | Tasks 2, 3, 4 | Parser puro + 5 efeitos + brush, **os três compilados** ao escrever o plano. `AcrylicBrush` permanece como fallback documentado e contado. |
| Variáveis de estilo (`=>` e `{{}}`) | Tasks 5, 6 | Avaliador puro **compilado e executado** contra 23 casos; a parte viva **compilada**. |
| Reciclagem do `ItemsRepeater` | Task 7 | Spike primeiro, com pergunta e roteiro exatos; pode terminar sem código, e o plano diz que essa é uma saída legítima. |
| `SendMessageTimeoutW` | Task 1 | Feito, com a análise do tempo de vida do `RunParam`. |
| Resto herdado do ledger | Task 1 | `NumChildren` e os dois nits de comentário. |

**O que este plano NÃO verificou e quem vai pagar se estiver errado**

1. **Que o grafo de efeitos é aceito pelo compositor do explorer.** O código compila e é a transcrição do upstream, mas `CreateEffectFactory` só rejeita (ou aceita) em runtime. *Custo:* todo blur cai para `AcrylicBrush` — visualmente igual ao Plano 3, com `blur fallbacks` no log dizendo exatamente isso. O smoke da Task 4 é o que pega, e o item 3 do ciclo de teste manda registrar em vez de seguir.
2. **Que `ActualWidth`/`ActualHeight` chegam com valor útil no momento do primeiro relatório.** É plausível que o elemento ainda não tenha passado por layout, e a captura semeie 0. *Custo:* o primeiro apply usa 0 e o `SizeChanged` corrige logo depois — visível como um frame errado. Se for pior que isso, o log `dynamic ... unresolved for now` em regime (item 14 do smoke) é o sinal.
3. **Que a reciclagem existe.** Não verificável estaticamente; é literalmente o Step 0 da Task 7.
4. **Que `DataContextChanged` é o sinal certo.** Idem; o spike mede antes de o código existir.
5. **Que `winrt::implements` + `XamlCompositionBrushBaseT` convivem com a fronteira COM do TAP em runtime.** Compilam; o refcount de um `XamlBlurBrush` guardado em `prop.values` e simultaneamente referenciado pelo XAML é a parte que só o smoke exercita.

**Riscos de desenho que o revisor deve julgar**

- **Estado de variáveis sem escopo por `XamlRoot`** (Task 6). Justificado por medição (nenhum tema captura o mesmo nome duas vezes) e por desenho (uma thread por superfície). *Custo se errado:* um número lido da superfície errada; nunca crash; conserto é um nível de mapa.
- **Sem troca automática para o fallback em economia de energia** (Task 4). Justificado por medição (0 de 272 tags usam `FallbackColor`, e sem ela o próprio upstream nunca troca). *Custo se errado:* num tema futuro com `FallbackColor`, o blur continua ligado com economia de energia — consumo, não corretude.
- **Gatilho de reciclagem em todo elemento estilizado**, não só em itens de repeater (Task 7). *Custo se errado:* restore+re-match redundante; o ciclo de teste manda voltar ao spike se o log mostrar repetição em rajada.
- **Cadeia de ancestrais recalculada em vez de cacheada** (Task 6). Mais simples que o grafo de nós do upstream e imune a spine obsoleta; *custo:* O(profundidade) por mudança de captura, com profundidade ~20 e no máximo algumas dezenas de capturas — irrelevante perto de um layout pass, mas é o primeiro lugar a olhar se o perfil da thread de UI piorar.

**Ordem e dependências**

`T1` é independente. `T2 → T3 → T4` (spec → efeitos → brush). `T5 → T6` (avaliador → uso). `T7` depende de `T6` só para ter um tema com estilos por item para observar. Nenhuma task deixa o repositório num estado que não compila ou que regride visualmente: T2, T3 e T5 são aditivas e invisíveis; T4 e T6 são as que mudam pixel, cada uma com seu smoke.
