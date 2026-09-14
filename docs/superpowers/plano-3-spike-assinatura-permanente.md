# Spike — a queda do `explorer.exe` na assinatura permanente

Branch: `spike-standing-crash`, a partir de `plano-3-aplicar-estilos` em `b919419`.
Um commit por experimento. Cada rodada: editar, buildar pelo VsDevCmd, reiniciar o
Explorer, `taskbar-styler.exe load`, observar 60 s, conferir
`%LOCALAPPDATA%\TaskbarStyler\log.txt` e o Visualizador de Eventos.

Duas descobertas de método logo de saída, que mudam como se lê o relatório da Task 4:

- **O log é confiável.** `src/tap/log.cpp:110` abre e fecha o arquivo a cada linha
  (`_wfopen_s` … `fclose`). Não há buffer para perder. Então a ausência de
  `subscription started` é prova de que `AdviseVisualTreeChange` não retornou —
  não é linha perdida na queda.
- **Nem toda falha é queda.** A janela de 30 s da Task 4 é curta demais e
  `explorer.exe` vivo não quer dizer saudável. Em três dos cinco experimentos o
  processo continua vivo com `Responding: False` — a thread de UI **trava**, não
  morre. Sem checar `Responding` esses casos passam por "sobreviveu".

Por isso o `STYLER_LOG` também importa: `src/tap/log.h:35` testa
`LogEnabled(level)` **antes** de avaliar os argumentos. Em nível Info o
`winrt::get_class_name(element)` do stub nunca é chamado. A Tentativa 2 da Task 4
rodou em Info e mesmo assim caiu — `get_class_name` já estava descartado como
causa antes do primeiro experimento.

## Controle (E0) — `b919419` intacto

Sem diff. Reproduz exatamente o relatado.

```
[15:20:09.700 18708/21728] INFO SetSite(000000001C1AF700)
[15:20:09.701 18708/21728] INFO loaded into C:\WINDOWS\explorer.exe
[15:20:09.702 18708/21728] INFO diagnostics session open
[15:20:09.702 18708/21728] INFO initialized for thread 21728
[15:20:09.703 18708/21728] INFO host watch started
[15:20:09.710 18708/21728] INFO snapshot: 495 elements
[15:20:09.719 18708/21728] INFO tree exported to C:\Users\Unknown\AppData\Local\TaskbarStyler\visual-tree.txt
[15:20:09.720 18708/21728] INFO handles released so far: 988
[15:20:09.720 18708/21728] INFO init data: C:\Users\Unknown\Desktop\Nova pasta\taskbar-styler\build\src\cli\themes
[15:20:09.722 18708/21728] INFO winrt ok: Windows.UI.Xaml.Controls.Grid
```

**CRASHED.** Sem `subscription started`. Winlogon 1002 às 15:20:14, 5 s depois do
load. Confirma que o harness detecta a queda.

## E1 — `SetSite` sem o snapshot do Plano 2 e sem `ProbeWinRt`

Diff (`src/tap/tap_boundary.cpp`): `ExportTreeToFile` e o bloco
`AcquireSession()`/`ProbeWinRt` passam a ficar sob `if constexpr (false)`.
`StandingCallback` e `StartSubscription` idênticos a `b919419`.

```
[15:21:59.209 19304/24184] INFO SetSite(000000001DA69100)
[15:21:59.210 19304/24184] INFO loaded into C:\WINDOWS\explorer.exe
[15:21:59.210 19304/24184] INFO diagnostics session open
[15:21:59.212 19304/24184] INFO initialized for thread 24184
[15:21:59.213 19304/24184] INFO host watch started
[15:21:59.213 19304/24184] INFO init data: C:\Users\Unknown\Desktop\Nova pasta\taskbar-styler\build\src\cli\themes
```

**CRASHED.** Sem `subscription started`. Winlogon 1002 às 15:22:35 — 36 s depois do
load, fora da janela de 30 s (foi o que motivou passar a observar 60 s e a checar
`Responding`).

→ As diferenças **1 (snapshot) e 2 (probe) não são a causa.**

## E2 — snapshot mantido, callback adia o lookup

Diff (`src/tap/change_subscription.cpp`): `SetSite` restaurado.
`OnVisualTreeChange` não chama mais nada de WinRT no ramo `Add` — só empilha
`{handle, type}` num `thread_local std::vector<DeferredAdd>`, como o
`SnapshotCallback` do `tree_export.cpp` faz. Os `QueueRelease` continuam. O
`GetIInspectableFromHandle` + `GetOrCreateElementId` + `OnElementAdded` passam
para `StartSubscription`, logo depois de `AdviseVisualTreeChange` retornar, na
mesma thread.

```
[15:25:38.685 15344/21904] INFO snapshot: 495 elements
[15:25:38.695 15344/21904] INFO tree exported to C:\Users\Unknown\AppData\Local\TaskbarStyler\visual-tree.txt
[15:25:38.696 15344/21904] INFO handles released so far: 988
[15:25:38.696 15344/21904] INFO init data: C:\Users\Unknown\Desktop\Nova pasta\taskbar-styler\build\src\cli\themes
[15:25:38.698 15344/21904] INFO winrt ok: Windows.UI.Xaml.Controls.Grid
[15:25:38.703 15344/21904] INFO subscription started on thread 21904
[15:25:38.705 15344/21904] INFO deferred flood: 495 adds
```

**CRASHED** — mas o resultado importante é o que apareceu antes:
`subscription started on thread 21904`. **O `AdviseVisualTreeChange` completou**,
com os 495 elementos da inundação inicial percorridos. A morte foi no laço
diferido: `deferred flood resolved` nunca saiu.

→ A **reentrância dentro do walk não é a causa.** Tirando o WinRT de dentro do
relatório, o walk fica limpo — e a falha vai junto com o lookup para um laço
comum, sem reentrância, sem walk, sem frame de callback do XAML.

## E3 — callback diferido (E2) sem o snapshot, `ProbeWinRt` mantido

Célula que faltava do 2×2. Diff: o de E2, mais `if constexpr (false)` só no
`ExportTreeToFile`. O `ProbeWinRt` fica.

```
[15:29:15.692 24392/23132] INFO host watch started
[15:29:15.692 24392/23132] INFO init data: C:\Users\Unknown\Desktop\Nova pasta\taskbar-styler\build\src\cli\themes
[15:29:15.693 24392/23132] INFO winrt ok: Windows.UI.Xaml.Controls.Grid
[15:29:15.698 24392/23132] INFO subscription started on thread 23132
[15:29:15.698 24392/23132] INFO deferred flood: 503 adds
```

**HUNG.** O processo não morreu em 60 s, mas ficou `Responding: False`, travado no
laço diferido; `deferred flood resolved` nunca saiu. Precisou de
`taskkill /f /im explorer.exe`.

→ Confirma E1 pelo outro lado: **o snapshot não é a causa**, e o caminho de lookup
falha (morrendo ou travando) mesmo completamente fora do walk.

## E4 — sem referência para `t_ids` atravessando `make_weak`, sem `get_class_name`

`SetSite` e `StandingCallback` voltam a ser idênticos a `b919419`. Mudam só:

- `src/tap/element_registry.cpp`: `GetOrCreateElementId` não segura mais um
  `Entry&` atravessando `winrt::make_weak`. Procura com `find`, monta o
  `weak_ref` numa local e só então insere em `t_ids`.
- `src/tap/style_engine.cpp`: `winrt::get_class_name` fora do stub.

```
[15:33:27.413 17068/9472] INFO snapshot: 509 elements
[15:33:27.422 17068/9472] INFO tree exported to C:\Users\Unknown\AppData\Local\TaskbarStyler\visual-tree.txt
[15:33:27.422 17068/9472] INFO handles released so far: 1016
[15:33:27.422 17068/9472] INFO init data: C:\Users\Unknown\Desktop\Nova pasta\taskbar-styler\build\src\cli\themes
[15:33:27.424 17068/9472] INFO winrt ok: Windows.UI.Xaml.Controls.Grid
```

**HUNG** (`Responding: False`), não CRASHED. Sem `subscription started`.

Como em Info não dá para ver até onde o walk foi, rodei **E4b** — mesmo diff, com
`g_level` em Debug por uma rodada:

```
[15:35:58.859 18016/11448] DBG  add 68 (Windows.UI.Xaml.Controls.Border)
[15:35:58.859 18016/11448] DBG  add 69 (Windows.UI.Xaml.Controls.ContentPresenter)
[15:35:58.859 18016/11448] DBG  add 70 (Windows.UI.Xaml.Controls.Border)
[15:35:58.860 18016/11448] DBG  add 71 (Windows.UI.Xaml.Controls.ContentPresenter)
[15:35:58.860 18016/11448] DBG  add 72 (SystemTray.StackListView)
[15:35:58.860 18016/11448] DBG  add 73 (Windows.UI.Xaml.Controls.Border)
```

72 linhas `add`, nenhuma `remove`, nenhum `subscription started`, e então silêncio
com a thread travada. **O mesmo ponto em que o baseline morria** (76 `add` na
Tentativa 1 da Task 4). O fix do registry tira o AV mas não passa do mesmo
elemento.

→ A **diferença 4 (`t_ids`) não é a causa** — é um bug real e latente, e vale
corrigir, mas corrigi-lo só troca a morte por travamento no mesmo lugar. E
`get_class_name`, já descartado pelo curto-circuito do `STYLER_LOG`, confirma-se
irrelevante.

## E5 — `Advise` disparado pelo message loop, não de dentro do `SetSite`

Última diferença estrutural do call site que restava: em `b919419` o
`AdviseVisualTreeChange` roda na pilha do `SetSite`, numa thread de UI que ainda
não voltou da chamada COM e portanto não está bombeando mensagens. Diff
(`src/tap/tap_boundary.cpp`): `StartSubscription()` passa por
`DispatcherQueue::GetForCurrentThread().TryEnqueue(...)` — mesma thread, já
bombeando. Rodado com `g_level` em Debug.

```
[15:40:20.982 23148/15012] INFO subscription enqueued
...
[15:40:21.009 23148/15012] DBG  add 71 Windows.UI.Xaml.Controls.ContentPresenter (Windows.UI.Xaml.Controls.ContentPresenter)
[15:40:21.009 23148/15012] DBG  add 72 SystemTray.StackListView (SystemTray.StackListView)
[15:40:21.009 23148/15012] DBG  add 73 Windows.UI.Xaml.Controls.Border (Windows.UI.Xaml.Controls.Border)
```

**HUNG.** 72 linhas `add`, última `add 73` logo depois de
`add 72 SystemTray.StackListView`, `Responding: False`. Idêntico a E4b, elemento
por elemento. Sair da pilha do `SetSite` não muda nada.

## Resumo

| Exp. | snapshot | probe | lookup | registry | resultado | walk terminou? |
|------|----------|-------|--------|----------|-----------|----------------|
| E0   | sim | sim | no walk | `b919419` | CRASHED (5 s)  | não |
| E1   | não | não | no walk | `b919419` | CRASHED (36 s) | não |
| E2   | sim | sim | diferido | `b919419` | CRASHED | **sim**, 495 |
| E3   | não | sim | diferido | `b919419` | HUNG    | **sim**, 503 |
| E4   | sim | sim | no walk | corrigido | HUNG, para em ~73 | não |
| E5   | sim | sim | no walk (via message loop) | `b919419` | HUNG, para em ~73 | não |

## O que isso significa

**Nenhuma das quatro diferenças levantadas é a causa.** Cada uma foi removida ou
neutralizada e a falha continuou:

1. **O snapshot do Plano 2 não é a causa.** E1 e E3 rodaram sem ele e falharam
   igual. A hipótese de que o `UnregisterInstance` em massa entregaria handles
   podres ao segundo `Advise` está descartada: sem nenhum `ReleaseHandle` prévio o
   comportamento é o mesmo.
2. **O `ProbeWinRt`/`GetUiLayer` não é a causa.** E1 rodou sem ele e falhou igual.
3. **A reentrância do lookup dentro do walk não é a causa** — e este é o achado que
   derruba a hipótese do implementador. Em E2 e E3 o callback não toca em WinRT
   nenhum: o `AdviseVisualTreeChange` **retorna com sucesso**, com os 495/503
   elementos percorridos e `subscription started` no log. A falha não some — ela
   **acompanha o lookup** para dentro de um laço `for` comum, rodando depois do
   `Advise`, sem reentrância nenhuma. Não é *onde* o lookup roda; é o lookup.
4. **O padrão `Entry&` atravessando `make_weak` não é a causa**, embora seja um bug
   real (uma referência para dentro de um `unordered_map` mantida através de uma
   chamada que pode reentrar e reordenar o mapa — vale corrigir de qualquer forma).
   Corrigido em E4, o AV vira travamento **no mesmo elemento**.

O que os experimentos **isolaram positivamente** é o ponto da falha, e ele é
notavelmente determinístico. Em toda configuração que resolve os objetos dentro do
walk, a thread de UI para no mesmo lugar: depois de ~72 elementos, imediatamente
após `SystemTray.StackListView` (o `#IconStack` do `visual-tree.txt:119`, um
`ListView` virtualizante, cujo `ItemsPresenter` contém `ContentControl[1]`,
`StackPanel`, `ContentControl[2]`) e do `Border` seguinte. Bate com os 76 `add` da
Task 4 e com o AV que o Visualizador de Eventos registrou **dentro de
`SystemTray.dll`** (`0xc0000005`, offset `0xe86f`).

Contraste que fecha o argumento: o snapshot do Plano 2 percorre a árvore **inteira**
(495–509 elementos) sem tropeço nenhum — porque ele só copia campos primitivos do
relatório e nunca chama `GetIInspectableFromHandle`. A diferença entre percorrer
509 elementos em 9 ms e travar no 74º é exatamente resolver o objeto.

### Portanto: inconclusivo quanto à causa raiz

Os experimentos **excluíram** as quatro diferenças propostas e **localizaram** a
falha num único elemento do subtree do SystemTray, mas **não isolaram qual chamada**
do caminho de lookup a provoca, nem por que ela mata/trava. Não escolho uma causa
que os experimentos não isolaram.

Restam três candidatos, nenhum testado:

- **`InspectableFromRaw` (`src/tap/winrt_common.h:37`).** É a única diferença de
  código que sobrou entre nós e o upstream no caminho de lookup. Nós fazemos
  `com_ptr::attach(raw)` seguido de `.as<wf::IInspectable>()` — um
  `QueryInterface(IID_IInspectable)` **extra** sobre o objeto. O upstream
  (`vendor/upstream/windows-11-taskbar-styler.wh.cpp:10988`) escreve direto no
  `wf::IInspectable` com `winrt::put_abi`, sem QI nenhum. Um QI a mais num item
  virtualizado de `SystemTray.StackListView` é o suspeito mais barato de eliminar.
- **Realização forçada de item virtualizado.** `StackListView` é um `ListView`;
  resolver o handle de um item ainda não realizado pode forçar o XAML a realizá-lo
  de dentro do próprio walk. Contra essa teoria: um laço infinito de realização
  produziria mais linhas `add`, e o log **para**. A favor: explica por que é sempre
  esse subtree e por que o AV é em `SystemTray.dll`.
- **Um deadlock genuíno de um componente do SystemTray** ao ser inspecionado por
  diagnostics enquanto ele mesmo está no meio de um layout.

### O menor próximo passo

**Primeiro, alinhar `InspectableFromRaw` com o upstream** — é uma função, três
linhas, e elimina a única diferença de código conhecida no caminho que falha:

```cpp
inline wf::IInspectable InspectableFromHandle(IXamlDiagnostics* diag,
                                              InstanceHandle handle) {
    wf::IInspectable obj{nullptr};
    winrt::check_hresult(diag->GetIInspectableFromHandle(
        handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(obj))));
    return obj;
}
```

Rodar com `g_level` em Debug e olhar uma coisa só: **passa de `add 73`?** Se
passar, era o QI extra e a assinatura permanente fica segura com essa mudança.
Se travar no mesmo ponto, o problema é o próprio
`GetIInspectableFromHandle` sobre aquele elemento, e aí o menor caminho para uma
assinatura permanente segura é **não resolver o objeto durante a inundação
inicial** — a forma do E2, que é a única configuração em que o
`AdviseVisualTreeChange` comprovadamente completa: gravar `{handle, parent, type}`
no walk, e resolver depois, com um `try/catch` por elemento e uma lista de tipos
a pular (a começar por `SystemTray.*`) para que um elemento venenoso derrube só a
si mesmo.

### Independente do resultado: corrigir o `element_registry`

O padrão de E4 deve entrar de qualquer forma. Não é a causa desta queda, mas
`Entry& entry = t_ids[handle]` mantido através de `winrt::make_weak(element)` é
um use-after-free esperando reentrância: qualquer relatório que chegue durante o
`make_weak` insere ou apaga em `t_ids`, reordena o mapa, e o
`entry.element = ...` seguinte escreve em memória liberada. É um diff de dez
linhas (commit `9a3c6d0`) e tira uma classe inteira de corrupção de heap do
caminho — o tipo de bug que aparece como AV num módulo sem relação nenhuma.

## E6 — `InspectableFromRaw` sem o `QueryInterface` extra

O "primeiro passo" recomendado acima, testado. Forma do baseline em tudo o mais
(resolve dentro do walk). Diff (`src/tap/winrt_common.h`): `com_ptr::attach` +
`.as<wf::IInspectable>()` viram `winrt::attach_abi(obj, raw)` — adota o ponteiro
sem chamada COM nenhuma, eliminando o `QueryInterface(IID_IInspectable)` que o
upstream não faz. Rodado em Debug.

```
[15:48:26.989 3156/11804] DBG  add 70 Windows.UI.Xaml.Controls.Border (Windows.UI.Xaml.Controls.Border)
[15:48:26.989 3156/11804] DBG  add 71 Windows.UI.Xaml.Controls.ContentPresenter (Windows.UI.Xaml.Controls.ContentPresenter)
[15:48:26.989 3156/11804] DBG  add 72 SystemTray.StackListView (SystemTray.StackListView)
[15:48:26.990 3156/11804] DBG  add 73 Windows.UI.Xaml.Controls.Border (Windows.UI.Xaml.Controls.Border)
```

**CRASHED.** 72 linhas `add`, sem `subscription started`, parando no mesmíssimo
elemento. **O QI extra não era a causa** — a recomendação anterior estava errada, e
o experimento a derrubou.

## E7 — E6 mais pular `SystemTray.*`, e logar o tipo antes de resolver

Diff (`src/tap/change_subscription.cpp`), sobre E6: no ramo `Add`, logar
`report <type>` **antes** de resolver (a última linha do log passa a nomear o
elemento que mata — o upstream loga o tipo na entrada pelo mesmo motivo,
`vendor:11110`); e pular `GetIInspectableFromHandle` para todo `element.Type`
começando com `SystemTray.`, ainda enfileirando os handles para release.

```
[15:51:05.239 11756/24104] DBG  add 56 Windows.UI.Xaml.Controls.ContentPresenter (Windows.UI.Xaml.Controls.ContentPresenter)
[15:51:05.241 11756/24104] DBG  report SystemTray.StackListView
[15:51:05.241 11756/24104] DBG  skipped SystemTray.StackListView
[15:51:05.241 11756/24104] DBG  report Windows.UI.Xaml.Controls.Border
[15:51:05.242 11756/24104] DBG  add 57 Windows.UI.Xaml.Controls.Border (Windows.UI.Xaml.Controls.Border)
[15:51:05.242 11756/24104] DBG  report Windows.UI.Composition.SpriteVisual
```

**CRASHED.** 72 `report`, 56 `add`, 14 `skipped`, sem `subscription started`.

**O elemento que mata é um `Windows.UI.Composition.SpriteVisual`** — e não é do
SystemTray. Pular o subtree do SystemTray não salva nada: o `StackListView` foi
pulado sem incidente, o `Border` seguinte resolveu normalmente (`add 57`), e a
morte veio no `SpriteVisual` logo depois. Em todas as rodadas anteriores era esse
mesmo `SpriteVisual` — ele só era invisível porque o log só saía *depois* da
resolução.

## A causa, achada no upstream

Um `Windows.UI.Composition.SpriteVisual` não é elemento XAML: é um visual de
DirectComposition. E o upstream trata disso explicitamente — está tudo no
comentário de `vendor/upstream/windows-11-taskbar-styler.wh.cpp:10905-10914`:

> "…without any locking whenever a DirectComposition visual is added, so any
> explorer UI thread which adds one, Task View for example, **corrupts the heap**
> while another thread is in the same code. Only element mutations are needed
> here, and those are reported by an unrelated code path, so the composition
> diagnostics are kept from being created at all:
> `XamlDiagnostics::CreateCompVisualDiag` skips them when the
> `HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag` value is 1.
> `Windows.UI.Xaml.dll` reads and caches the value once, from within
> `AdviseVisualTreeChange`, so answering that single read is enough."

Isto é exatamente o nosso bug, descrito pelo autor do upstream: **as
diagnostics de composition corrompem o heap**. Corrupção de heap explica tudo o
que vimos e que nenhuma hipótese anterior explicava — o AV num módulo sem
relação nenhuma (`SystemTray.dll+0xe86f`), a alternância entre morte e
travamento, e o atraso variável (5 s, 36 s) entre o load e o Winlogon 1002.

São **duas** proteções do upstream que `b919419` não tem, e é por isso que "o
upstream faz a mesma coisa e não cai" estava errado — ele não faz a mesma coisa:

1. **`g_reportCompositionDiagAsDisabled`** (`vendor:11023`), ligado só em volta da
   chamada a `AdviseVisualTreeChange`. Com ele ligado, os hooks de
   `RegOpenKeyExW`/`RegQueryValueExW` (`vendor:19985`, `vendor:20006`) fazem o
   `Windows.UI.Xaml.dll` enxergar `DisableCompositionDiag = 1` na única leitura
   que ele faz, de dentro do próprio `Advise`. Resultado: as diagnostics de
   composition **nunca são criadas** e nenhum `SpriteVisual` é reportado. Nós
   recebemos os `SpriteVisual` e morremos neles.
2. **O `Advise` roda numa thread nova** (`vendor:11017-11033`), não na thread de
   UI. O comentário do upstream: *"Calling AdviseVisualTreeChange from the current
   thread causes the app to hang in `Advising::RunOnUIThread` sometimes. Creating
   a new thread and calling it from there fixes it."* É literalmente o nosso
   **HUNG** (`Responding: False`) de E3/E4/E5 — e explica por que o E5 falhou: eu
   enfileirei no `DispatcherQueue` da **mesma** thread; precisa ser outra thread.

Note também, em `vendor:11013`, que a chamada direta está **comentada** no
upstream — a forma de `b919419` é exatamente a que o upstream tentou primeiro e
abandonou.

### Correção mínima, revisada

A recomendação anterior (`InspectableFromRaw`) fica retirada: E6 a refutou. O que
`b919419` precisa são as duas proteções acima, nessa ordem de importância:

1. **Impedir que as diagnostics de composition sejam criadas.** É o que remove a
   corrupção de heap, ou seja, a causa. O mecanismo do upstream é hook de
   `RegOpenKeyExW`/`RegQueryValueExW` — o que colide com a regra "sem APIs de
   injeção" deste repositório, então **é decisão do controller**, não minha. As
   alternativas sem hook são: escrever
   `HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag = 1` de verdade
   (precisa de admin, e é estado global da máquina), ou aceitar que a assinatura
   permanente não é viável sem uma das duas. **Filtrar no nosso callback não
   resolve**: pelo comentário do upstream a corrupção acontece dentro do
   `XamlDiagnostics::CreateCompVisualDiag`, antes de sermos chamados — E7 é a
   prova empírica disso, já que morremos com o `SpriteVisual` apenas *reportado*.
2. **Chamar `AdviseVisualTreeChange` de uma thread nova**, como `vendor:11017`,
   e não da pilha do `SetSite`. Isso é um `CreateThread` de dez linhas, não usa
   nenhuma API proibida, e é o que remove o travamento em
   `Advising::RunOnUIThread`. Vale fazer de qualquer forma.
3. **Manter o fix do `element_registry`** (commit `9a3c6d0`), pelos motivos já
   dados — embora agora se saiba que o upstream tem exatamente o mesmo padrão
   `Entry&` (`vendor:11853`), então ele não é diferença nossa. É defeito latente
   compartilhado, de prioridade menor.

## E8a — a forma sem hook: pular composition visuals + Advise numa thread nova

Sobre E6 (o `attach_abi` fica, mesmo não tendo sido a causa). Diff:

- `src/tap/change_subscription.cpp`: no ramo `Add`, todo `element.Type` que comece
  com `Windows.UI.Composition.` **não é resolvido** — loga `skipped composition
  visual` em Debug, e os handles vão para a fila de release como em qualquer
  outro `Add`. O skip de `SystemTray.*` do E7 saiu.
- `StartSubscription`: o `AdviseVisualTreeChange` roda numa thread criada com
  `CreateThread`, espelhando `vendor:11017-11030` — `AddRef` do callback para a
  thread, `Release` quando ela termina, HRESULT logado. `StartSubscription`
  retorna depois de criar a thread e **não espera mais a inundação inicial**.

Uma rodada em Debug, com Iniciar, central de notificações e Task View abertos uma
vez cada:

```
[16:00:08.784 23404/10380] INFO advise thread created
[16:00:09.169 23404/22504] INFO subscription started on thread 22504
[16:01:10.885 23404/10380] INFO drained 40 handles, 0 held (2623 released so far)
```

**SURVIVED.** 1711 `report`, 1547 `add`, **157 composition visuals pulados**, 577
`remove`, 9 drenos, 2623 handles liberados, **0 linhas `ERR`**. Explorer
`Responding: True` no fim, taskbar funcionando.

É a primeira configuração em que a assinatura permanente **existe de fato**: o
flood completa, os relatórios continuam chegando depois dele (os 577 `remove` e os
drenos vêm da interação), e o `held` volta a zero.

As duas mudanças são necessárias e atacam coisas diferentes: pular os composition
visuals remove a **morte determinística** (resolver um handle de composition-diag),
e a thread nova remove o **travamento** em `Advising::RunOnUIThread`.

**Ressalva de método:** o `TryEnqueue` do E5 em `tap_boundary.cpp` nunca foi
revertido, então E6, E7 e E8a rodaram todos com ele. Para o E8a é redundante — o
`SetSite` enfileira e o `StartSubscription` cria a thread — mas o diff testado
inclui os dois. Quem for portar isso para `b919419` deve tirar o `TryEnqueue`:
a thread do `CreateThread` é o que importa, e E5 sozinho (só `TryEnqueue`) travou.

## E8b — o mesmo binário com `DisableCompositionDiag = 1`

Antes: `reg query "HKLM\Software\Microsoft\XAML\Debug" /v DisableCompositionDiag`
→ a chave **não existia** (`HKLM\Software\Microsoft\XAML` existe e está vazia),
exatamente como o comentário do upstream diz ("The key usually doesn't exist").

O valor foi então escrito com consentimento do usuário, via processo elevado
(prompt do UAC aceito):

```
reg add "HKLM\Software\Microsoft\XAML\Debug" /v DisableCompositionDiag /t REG_DWORD /d 1 /f
```

Confirmado:

```
HKEY_LOCAL_MACHINE\Software\Microsoft\XAML\Debug
    DisableCompositionDiag    REG_DWORD    0x1
```

Mesmo binário do E8a, Explorer reiniciado, uma rodada em Debug com as mesmas três
interações:

| métrica | E8a (sem a chave) | E8b (com a chave) |
|---|---|---|
| `report Windows.UI.Composition.*` | 157 | **0** |
| `skipped composition visual` | 157 | **0** |
| `report` no total | 1711 | 792 |
| `add` | 1547 | 790 |
| `subscription started` | 1 | 1 |
| `ERR` | 0 | 0 |
| resultado | SURVIVED | **SURVIVED** |

**Confirma o mecanismo do upstream ponto a ponto.** Com a chave em 1, o
`Windows.UI.Xaml.dll` nunca cria as diagnostics de composition e **nenhum**
`Windows.UI.Composition.*` é reportado — o filtro do E8a fica sem nada para
pular. É a diferença entre remover o sintoma (E8a: nós ignoramos o que chega) e
remover a fonte (E8b: eles nunca são criados, e portanto a corrida de heap
descrita em `vendor:10905` também deixa de existir).

Os dois são complementares, não alternativos: o filtro do E8a é o que torna a
assinatura segura **sem** exigir escrita em HKLM; a chave é o que fecha também a
corrida probabilística de heap. Com a chave, o filtro do E8a vira defesa em
profundidade barata.

### Segundo `load` sem reiniciar o Explorer — SURVIVED, mas com defeito

```
[16:03:45.144 21172/24916] INFO OpenDiagnostics called with a session already open; closing and reopening
[16:03:45.156 21172/24916] INFO snapshot: 1135 elements
[16:03:45.173 21172/24916] INFO subscription enqueued
```

Não caiu nem travou. Mas **nem `advise thread created` nem `subscription started`
aparecem**: `StartSubscription` retorna `S_FALSE` logo na primeira linha
(`if (g_subscription->load())`), porque a assinatura anterior continua
registrada. Só o ramo `!site` do `SetSite` chama `StopSubscription`, e um segundo
`load` nunca passa por ele.

O resultado é que, depois do segundo `load`, o `g_subscription` aponta para um
`service`/`callback` presos à sessão de diagnostics que o
`OpenDiagnostics` acabou de **fechar e reabrir** — a assinatura permanente está
morta e nenhuma nova consegue subir. Não derruba o shell, mas o TAP fica
funcionalmente inerte até o Explorer reiniciar. É um defeito à parte desta
investigação, e vale uma nota para o Plano 3: `SetSite` com site não-nulo deveria
chamar `StopSubscription()` antes de reabrir a sessão.

### Estado do registro ao fim do spike

`HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag` **continua em 1** —
foi escrito nesta sessão, com consentimento, e não foi revertido. É configuração
global da máquina e afeta as diagnostics XAML de qualquer processo, não só o
Explorer. Se o Plano 3 não for depender dela, remover com
`reg delete "HKLM\Software\Microsoft\XAML\Debug" /v DisableCompositionDiag /f`
(elevado).

## Ambiente

Todas as rodadas deixaram o shell recuperado com `taskkill /f /im explorer.exe`
seguido de `Start-Process explorer.exe`. Ao fim do spike o Explorer está rodando,
responsivo e **sem o TAP carregado**; `plano-3-aplicar-estilos` está limpo e a DLL
reconstruída a partir de `b919419`.

Não foi possível pegar dump: `WER LocalDumps` exige HKLM. Os eventos
`Application Error` das rodadas deste spike não foram registrados (só os
`Winlogon 1002`); o único AV com módulo e offset é o que a Task 4 já tinha achado
— `explorer.exe`, `0xc0000005` em `SystemTray.dll+0xe86f`.
