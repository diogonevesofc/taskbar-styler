# AGENTS.md — guia para agentes (Codex, Claude, humanos)

Leia este arquivo inteiro antes de tocar em qualquer coisa. Depois leia
`docs/STATUS.md` (estado atual) e, para o trabalho em curso, o plano citado lá.

## O que é

`taskbar-styler` restyliza a taskbar do Windows 11 **sem Windhawk**. É uma obra
derivada do mod `windows-11-taskbar-styler` (m417z, GPL-3.0), vendorizado em
`vendor/upstream/windows-11-taskbar-styler.wh.cpp` como referência de
comportamento — nunca como código a copiar cegamente.

Mecanismo: o Windows carrega nossa DLL COM dentro do `explorer.exe` via
`InitializeXamlDiagnosticsEx` (XAML Diagnostics TAP, o mesmo que o Live Visual
Tree do Visual Studio). A DLL recebe a árvore visual e aplica os estilos do tema.

```
src/core/   styler_core — C++20 puro, SEM Windows: seletores, regras, temas,
            constantes, resource variables, parser de <WindhawkBlur>, avaliador
            de {{...}} (Plano 3b). Testado com doctest em tests/core.
src/tap/    TaskbarStyler.Tap.dll — o TAP: fronteira COM (tap_boundary.cpp),
            assinatura permanente da árvore (change_subscription), registro de
            elementos e fila de liberação (thread_local), motor de estilos
            (style_engine), setters (property_setter), blur real (blur_effects,
            blur_brush), sessão de tema (theme_session). Testes em tests/tap.
src/cli/    taskbar-styler.exe — apply/reset/list/status/setup/unload.
themes/     55 temas em JSON, gerados de vendor/ por tools/extract_themes.py
            (o CI confere byte a byte que continuam iguais à fonte).
docs/       spec, planos, registros de decisões, smoke-test.md, STATUS.md.
```

## Restrições invioláveis (o CI verifica parte delas)

- **Nenhuma API de injeção, nenhum patch de código.** Proibidos:
  `VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread`,
  `SetThreadContext`, MinHook, Detours, hook inline. O único
  `SetWindowsHookEx` permitido é o `WH_CALLWNDPROC` de `thread_init.cpp`
  (despacho de mensagem, não patch).
- **Zero acesso à rede em runtime.** Nada de diálogo modal a partir do TAP.
- **`styler_core` permanece livre de Windows**: nada em `src/core/` inclui
  `<windows.h>` ou `winrt/`.
- **C++/WinRT só dos headers pré-gerados do SDK** (`WINRT_LEAN_AND_MEAN`,
  `WindowsApp.lib`). Sem NuGet, sem `cppwinrt.exe` no build, **sem projeção
  `Microsoft.UI.Xaml`**. `winrt_common.h` é o ÚNICO lugar com `#include <winrt/`.
- **Nenhuma interface que o SDK já declara é redeclarada à mão.**
- **Toda chamada WinRT no TAP fica em** `try { } catch (winrt::hresult_error const&) { } catch (...) { }`.
  Toda entrada chamável pelo XAML é catch-all e devolve `S_OK`. Uma exceção
  escapando de um callback é um crash dentro do explorer.
- **Estado por thread é `thread_local`** (registro de elementos, estado de
  customização, fila de liberação, variáveis de estilo). Cada superfície XAML da
  taskbar roda na própria thread; nada cruza threads.
- **O callback da assinatura permanente nunca libera handle.** Liberação só pela
  fila + dreno no `DispatcherQueue` da thread (200 ms de silêncio).
- **Nunca resolva handles de tipo `Windows.UI.Composition.*`** via
  `GetIInspectableFromHandle` — crash determinístico (ver
  `docs/superpowers/plano-3-spike-assinatura-permanente.md`).
- **`AdviseVisualTreeChange` roda numa thread própria (`CreateThread`)**, nunca
  na thread de UI: o lote inicial chega síncrono dentro da chamada.
- **Toda escrita de propriedade passa pelo `ModifyingGuard`** (RAII) de
  `property_setter.cpp`, e o original é capturado antes.
- **Brushes de blur (`XamlBlurBrush`) são por elemento** e nunca entram no
  cache de setters do motor (guardam o `Compositor` do elemento e chaves em
  `Resources()`).
- **Falhar fechado por regra, nunca pela metade por tema**: uma regra que não
  aplica é logada em `Error` e as demais seguem.
- **Licença GPL-3.0-or-later.** Todo `.h`/`.cpp`/`.py` novo começa com
  `// SPDX-License-Identifier: GPL-3.0-or-later` (não em CMake nem YAML).
- **Idioma:** identificadores e comentários de código em **inglês**;
  documentação, planos e mensagens de commit em **português**.

## Compilar e testar (Windows)

Requer Visual Studio 2026 Community (toolchain C++), Windows SDK 10.0.26100.0,
CMake + Ninja do próprio VS. x64 apenas. `cmake`/`ctest` normalmente NÃO estão
no PATH de um shell comum; use o ambiente do VS. Exemplo (ajuste o caminho do VS):

```
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=amd64 -no_logo && \"...\CMake\CMake\bin\cmake.exe\" -S . -B build -G Ninja"
cmd /c "... VsDevCmd.bat ... && ...\cmake.exe --build build"
cmd /c "... VsDevCmd.bat ... && ...\CMake\bin\ctest.exe --test-dir build --output-on-failure"
```

- Exigência: **0 warnings novos sob `/W4`** nos arquivos do projeto. Os únicos
  aceitos são os pré-existentes `C5285` (doctest vendorizado) e `C4002`
  (headers do SDK).
- Suítes: `core` e `tap` (contagens atuais em `docs/STATUS.md`); todas verdes
  antes de qualquer commit.
- `python -m pytest tools/ -q` para as ferramentas de conversão de temas.
- **`LNK1168` ao relinkar o TAP** = o explorer ainda tem a DLL carregada. Rode
  `build\src\cli\taskbar-styler.exe unload` ou reinicie o explorer. Reiniciar o
  explorer é autorizado neste projeto.
- Pré-requisito único de máquina: `taskbar-styler.exe setup` (UAC) grava
  `HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag=1`. Já está feito
  na máquina de desenvolvimento.
- Log do TAP: `%LOCALAPPDATA%\TaskbarStyler\log.txt`. Config:
  `%LOCALAPPDATA%\TaskbarStyler\config.json` (`"logLevel":"debug"` para ver
  cada mudança de estado visual).
- Smoke ao vivo: `docs/smoke-test.md`. Toda task que muda pixel exige o smoke
  e evidências (linhas do log, PID do explorer igual antes e depois, zero
  `ERR`). Para testar claro/escuro por script, a escrita no registro precisa
  de um `WM_SETTINGCHANGE` (`ImmersiveColorSet`) depois.

## Como o trabalho é organizado

1. **Spec**: `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md` é a
   autoridade. Planos argumentam a partir dela; conflitos resolvem-se contra ela.
2. **Planos**: `docs/superpowers/plans/`. Cada plano vira um branch
   (`plano-N-...`) de `main`, executado task a task. Cada task tem: arquivos,
   interfaces (o que consome/produz), steps com o código completo, ciclo de
   teste e a mensagem de commit. Um plano mesclado deixa em `docs/superpowers/`
   o registro de decisões (`plano-N-decisoes.md`) e os spikes.
3. **Por task**: ler o brief (a seção da task no plano), implementar
   exatamente o que ele diz, TDD onde há teste puro, build 0 warnings, suíte
   verde, **um commit por task** (`feat(tap): ...`, `fix(core): ...`,
   `docs: ...`), depois revisão independente (spec + qualidade) e rodada de
   correção. Divergir do brief é permitido quando o brief está errado —
   documente a divergência no commit e em `docs/STATUS.md`.
4. **Verifique fatos de API antes de afirmá-los.** Duas vezes neste projeto
   um método inexistente (`IVisualTreeService3::GetVisualRoots`) entrou num
   plano por memória e custou uma task bloqueada. Regra: `grep` no header do
   SDK (`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\`) ou no
   vendor, com `arquivo:linha`, antes de escrever código contra a API. Se só
   dá para saber em runtime, faça um spike pequeno primeiro.
5. **Mudou uma constante medida?** (contagens do corpus, número de temas,
   tags) Meça com `python` sobre `themes/*.json` e cite o número medido, não o
   lembrado.

## Git

- `main` recebe planos inteiros por merge fast-forward local depois da revisão
  final. Nunca commite direto em `main`.
- **Nunca `git push` sem o dono do repositório pedir explicitamente.** Nunca
  `--force`. Nunca `git clean -fdx` (apaga o scratch de execução).
- Não amende commits de outra pessoa/agente; crie um novo.
- `build/` é ignorado. `.superpowers/` (scratch de execução dos agentes) não é
  rastreado; por isso o estado vivo mora em `docs/STATUS.md` — atualize-o ao
  fechar cada task.

## Onde ler mais

- `README.md` — visão de usuário, estado por plano.
- `THEMES.md` — os 55 temas e créditos.
- `docs/superpowers/plano-3-decisoes.md` — decisões do motor de estilos,
  incluindo o que o Plano 3b herdou.
- `docs/superpowers/plano-3-spike-assinatura-permanente.md` — por que o crash
  0xC0000005 acontecia e como foi fechado.
