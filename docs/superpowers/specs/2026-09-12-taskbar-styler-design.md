# Taskbar Styler — documento de design

**Data:** 2026-09-12
**Status:** aprovado, aguardando plano de implementação

## 1. Contexto e motivação

O [windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods) é um mod do
Windhawk (autor: m417z, GPLv3) que customiza a taskbar do Windows 11 aplicando estilos à
árvore visual XAML do `explorer.exe`.

O objetivo deste projeto é obter a mesma capacidade sem depender de:

1. **do Windhawk** como plataforma de injeção e configuração;
2. **do ciclo de atualização de terceiros** — o usuário quer controlar o que roda dentro do
   processo do shell da própria máquina.

Duas observações que motivaram decisões deste design:

- O mod faz **telemetria**: a cada 24h baixa
  `https://github.com/.../stats-v6/<Tema>.txt` de dentro do `explorer.exe`, para contagem de
  popularidade de tema. Não coleta dado pessoal e é desativável por um setting não documentado,
  mas é tráfego de rede não solicitado saindo do processo do shell. Este projeto **não terá
  telemetria de nenhum tipo**.
- O mod injeta hooks inline (`CreateWindowExW`, `LoadLibraryExW`, `RegOpenKeyExW` e outros).
  Este projeto **não faz patch de código** — ver §5.2.

## 2. Escopo

### Dentro

- Aplicar qualquer um dos 54 temas prontos do mod original à taskbar.
- Trocar de tema em tempo real, sem reiniciar o explorer.
- Sobreviver a reinício do explorer, múltiplos monitores e flyouts.
- Desfazer o tema, restaurando os valores originais.
- Exportar a árvore visual, para o usuário conseguir corrigir seletores sozinho.

### Fora

- Escrever estilos customizados (`controlStyles`) pela interface. Os JSONs de tema são
  editáveis à mão, mas não há UI para isso.
- Portar funcionalidades acessórias do mod: `clickThroughTaskbar`,
  `xamlDiagnosticsHandling`, bloqueio de outras ferramentas de diagnóstico.
- Telemetria, atualização automática, instalador.

## 3. Viabilidade — resultado do spike

**Pergunta:** `InitializeXamlDiagnosticsEx` funciona cross-process, chamada de um processo
comum, sem privilégio elevado?

**Resposta: sim, verificada em 2026-09-12.**

Um exe console mínimo, não empacotado e sem elevação, chamou:

```c
InitializeXamlDiagnosticsEx(L"VisualDiagConnection1", pidDoExplorer, L"",
                            caminhoDaTapDll, CLSID_SpikeTAP, nullptr);
```

O log escrito de dentro do processo alvo:

```
[pid=5520] DLL_PROCESS_ATTACH em C:\WINDOWS\Explorer.EXE
[pid=5520] DllGetClassObject chamado
[pid=5520] CreateInstance chamado
[pid=5520] SetSite(site=...)
[pid=5520]   QI IXamlDiagnostics    -> hr=0x00000000
[pid=5520]   QI IVisualTreeService3 -> hr=0x00000000
```

`pid=5520` era o `explorer.exe` dono da `Shell_TrayWnd`. Conclusões:

- o Windows carrega a DLL no processo alvo por conta própria — sem `OpenProcess`,
  `VirtualAllocEx`, `WriteProcessMemory` ou `CreateRemoteThread`;
- `IVisualTreeService3`, a interface que muta a árvore visual, é obtida com sucesso;
- o COM é **registration-free**: passa-se o caminho da DLL e o CLSID, sem registry.

Código do spike: descartável, não vai para o repositório.

## 4. Arquitetura

Três componentes.

| Componente | Linguagem | Responsabilidade |
|---|---|---|
| `TaskbarStyler.Tray` | C# .NET 10, WinForms | Bandeja, lista de temas, config, vigia o explorer, dispara a carga |
| `TaskbarStyler.Tap` | C++ DLL | Dentro do explorer: recebe a árvore visual, casa seletores, aplica estilos |
| `themes/*.json` | dados | Os 54 temas, editáveis sem compilador |

O exe loader separado foi descartado após o spike: como `InitializeXamlDiagnosticsEx` é um
export nativo comum, o Tray chama por P/Invoke.

### 4.1 Fluxo de partida

```
Tray inicia
  └─ FindWindow("Shell_TrayWnd") → PID do explorer
      └─ P/Invoke InitializeXamlDiagnosticsEx(conn, pid, "", tap.dll, CLSID, installDir)
          └─ [dentro do explorer] Windows carrega tap.dll
              └─ DllGetClassObject → SetSite(IXamlDiagnostics)
                  └─ QI IVisualTreeService3
                      ├─ [Plano 2] Advise → lote inicial síncrono → Unadvise
                      │      └─ formata a árvore → exporta → libera handles
                      └─ [Plano 3] assinatura que fica de pé
                             └─ OnVisualTreeChange(Add, elemento)  ← por elemento
                                 └─ casa seletor → aplica estilo
```

A assinatura de notificação só entra no Plano 3: liberar handle de dentro do
callback exige o dreno adiado descrito no §7.2, e o Plano 2 não tem o que fazer
com o fluxo por elemento — ele percorre a árvore sob demanda e libera cada
handle dentro da própria travessia.

O laço de conexões (`VisualDiagConnection1`, `2`, …) é mantido: se o Visual Studio ou outra
ferramenta já tiver uma conexão aberta, é preciso procurar um slot livre.

### 4.2 Fronteira entre processos

Apenas duas coisas cruzam, ambas em uma direção (Tray → TAP):

1. **Configuração** — `%APPDATA%\TaskbarStyler\config.json`, contendo o nome do tema e o
   nível de log. Arquivo, não memória compartilhada: sobrevive a crash dos dois lados e é
   inspecionável com um editor de texto.
2. **Sinal de recarga** — um Event nomeado do Windows. O Tray sinaliza após escrever; o TAP
   acorda e reaplica. Escolhido em vez de vigiar o arquivo porque a troca de tema é um evento
   único e determinístico, sem debounce de gravação parcial.

O TAP nunca fala de volta com o Tray. Não há canal de retorno, protocolo, nem versionamento
de IPC. Se o TAP morre, o Tray descobre pelo desaparecimento do explorer.

### 4.3 Divisão do C++

| Alvo | Depende de | Testável |
|---|---|---|
| `styler_core` (lib estática) | nada além da std | sim, teste unitário comum |
| `TaskbarStyler.Tap` (DLL) | WinRT, XAML, COM | não, só no explorer |

`styler_core` contém os parsers, o modelo de tema, o carregador de JSON e a resolução de
constantes. O TAP é um adaptador fino. Lógica crescendo dentro do TAP é sinal de que deveria
estar no core.

## 5. Modelo de dados dos temas

### 5.1 Formato

Um arquivo por tema, `themes/<Id>.json`:

```json
{
  "id": "TranslucentTaskbar",
  "name": "TranslucentTaskbar",
  "author": "<preenchido pelo conversor a partir do readme upstream>",
  "constants": {
    "CommonBgBrush": "<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>"
  },
  "resourceVariables": {},
  "rules": [
    {
      "target": "Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill",
      "styles": ["Fill:=$CommonBgBrush"]
    },
    {
      "target": "Rectangle#BackgroundStroke",
      "styles": ["Visibility=Collapsed"]
    }
  ]
}
```

`"name"` sempre é igual a `"id"`: o conversor (`to_theme_json`) preenche os dois com o mesmo
valor para os 55 temas, porque o mod upstream não tem outra fonte de nome legível por humano —
não há um "nome bonito" separado para preencher.

### 5.2 `target` e `styles` permanecem strings opacas

A gramática real inclui `=`, `:=` (valor é XAML), `=>` (captura para variável),
`@VisualState`, `[3]` (índice), `[Prop=val]` (filtro de propriedade), `$Constante`,
`{{dinâmico}}`, `*` (curinga de ancestrais) e `:root`.

Decompor isso em campos JSON exigiria **dois parsers que precisam concordar** — o do conversor
e o do TAP. Mantendo string:

- o conversor é transliteração sem interpretação, portanto não pode estar sutilmente errado;
- seletores copiados de temas da comunidade funcionam sem edição;
- o parser existe em um lugar só.

### 5.3 Exceções deliberadas

- `constants` e `resourceVariables` são **mapas**, não listas `"Nome=valor"`. Aqui a string é
  comprovadamente um par chave-valor; o mapa detecta chave duplicada e é melhor de editar. A
  divisão é no **primeiro** `=`: o valor pode conter `=` (e contém, nos atributos XAML), o nome
  não pode. O conversor **falha em vez de adivinhar** em três casos: entrada sem `=`, nome
  vazio, ou chave duplicada dentro do mesmo tema.
- O caso `Squircle` — único condicional de runtime do mod inteiro — vira um campo opcional,
  mantendo o número mágico junto do tema a que pertence:

  ```json
  "osFeatureVariant": { "featureId": 48660958, "themeId": "Squircle_WeatherOnTheRight" }
  ```

- `<WindhawkBlur .../>` **mantém o nome**. Não é XAML real: é um pseudo-elemento que o mod
  parseia manualmente e converte em brush de acrílico, presente em 289 ocorrências. O TAP
  aceita `<WindhawkBlur>` e `<Blur>` como sinônimos. Renomear quebraria compatibilidade com
  todo tema já publicado.

### 5.4 O conversor e sua verificação

`tools/extract_themes.py` tem dois comandos de CLI:

- `convert` — tabelas C++ → 55 JSONs;
- `roundtrip` — prova que a conversão é sem perda: reconstrói cada tabela com a função interna
  `emit_theme_table` e compara byte a byte com o trecho original.

**Se `emit_theme_table(parse_source(fonte))` bate byte a byte com o trecho original, a
conversão é comprovadamente sem perda.** Isso transforma "a conversão está fiel?" de
julgamento em `diff`. É o primeiro teste escrito e roda no CI.

### 5.5 Parser JSON

nlohmann/json (MIT — compatível com GPLv3), buscado via `FetchContent` do CMake em tempo de
configuração, com `GIT_TAG` fixado no commit SHA completo correspondente à tag `v3.11.3` (não
a tag em si, que é mutável) — não é um single-header vendorizado no repositório. Um tema por
vez; o maior tem ~390 regras. Custo de parse irrelevante.

## 6. Ciclo de vida

### 6.1 Múltiplas superfícies

A taskbar não é uma janela só:

- `Windows.UI.Composition.DesktopWindowContentBridge`, filha de `Shell_TrayWnd` — principal;
- `XamlExplorerHostIslandWindow` — flyouts e taskbars de monitores secundários;
- `Shell_InputSwitchTopLevelWindow` — seletor de idioma.

Cada uma roda na própria thread e a inicialização precisa acontecer **dentro** daquela thread.
Mecânica copiada do mod: estado `thread_local` e despacho para a thread da janela via
`SetWindowsHookEx(WH_CALLWNDPROC)` com mensagem registrada.

### 6.2 Superfícies que aparecem depois — sem hooking

O mod detecta janelas novas com hook inline em `CreateWindowExW`. Este projeto usa
`SetWinEventHook(EVENT_OBJECT_CREATE)` filtrado pelo processo: API documentada, sem patch de
byte, sem dependência de MinHook ou Detours.

**Consequência:** a v1 não escreve um único byte na memória de código de nenhum processo. Nem
injeção, nem detour — apenas APIs sancionadas.

### 6.3 Reinício do explorer

Quando o explorer recria a taskbar, o Windows faz broadcast de
`RegisterWindowMessage("TaskbarCreated")` para todas as janelas de topo. O Tray já precisa
tratá-la para recolocar o próprio ícone, então a mesma mensagem dispara as duas ações. Sem
polling, sem WMI.

Poll de segurança a cada 30s como rede: `FindWindow` + comparar PID. Se o PID mudou e não há
TAP lá dentro, recarrega.

### 6.4 Desligar

- **Desativar tema** — o TAP reescreve os `originalValue` guardados por propriedade
  customizada, chama `UnadviseVisualTreeChange` e para. Funciona de forma confiável.
- **Remover a DLL do explorer** — não é oferecido. Descarregar uma DLL COM de um processo vivo,
  com callbacks do framework possivelmente em voo, é frágil por natureza. Após "Desativar" a
  DLL fica residente e **inerte**: sem advise, sem hook, sem timer. Sai no próximo reinício do
  explorer.

O menu da bandeja oferece "Desativar tema" e, separadamente, "Reiniciar o Explorer".

### 6.5 Falha na carga

Se `InitializeXamlDiagnosticsEx` falhar, o Tray **não tenta em loop**. Marca estado de falha,
muda o ícone, registra o `HRESULT`. Novo retry apenas no próximo `TaskbarCreated` ou por ação
do usuário. Um app que tenta injetar em loop infinito é indistinguível de um ataque.

## 7. Erros e observabilidade

### 7.1 Regra zero: não derrubar o explorer

Exceção não tratada no TAP mata o `explorer.exe`. Toda função chamável pelo XAML —
`SetSite`, `GetSite`, `OnVisualTreeChange`, `OnElementStateChanged`, `DllGetClassObject`,
`DllCanUnloadNow`, `QueryInterface`, `AddRef`/`Release`, `CreateInstance`, `LockServer` — é
catch-all que nunca deixa uma exceção escapar.

Isso não significa que toda função sempre retorna `S_OK`. A regra de "nunca devolver erro"
vale só para os **callbacks de evento** do XAML (`OnVisualTreeChange`,
`OnElementStateChanged`) e para `SetSite`: devolver erro aí faz o XAML parar de enviar
eventos, ou aborta a ativação inteira. Para as demais — `GetSite`, `QueryInterface`,
`CreateInstance`, `DllGetClassObject` — o `HRESULT` é uma resposta real que o chamador
usa para decidir o que fazer (por exemplo, `GetSite` sem site retorna `E_FAIL`, porque
`S_OK` significa "`*ppv` é válido"; devolver `S_OK` com um ponteiro nulo mentiria para
quem chamou e viola o contrato COM padrão de `IObjectWithSite`). Forçar `S_OK` nesses casos
esconderia erros sem nenhum benefício, já que eles não são o gatilho que faz o XAML calar.

As entradas do `IObjectWithSite` e da fábrica COM ficam todas em
`src/tap/tap_boundary.cpp`, para que "a fronteira está protegida?" se responda abrindo um
arquivo. A exceção são os callbacks do instantâneo da árvore, `OnVisualTreeChange` e
`OnElementStateChanged`, que moram em `src/tap/tree_export.cpp` junto do objeto que os
implementa: eles só existem entre um `Advise` e o `Unadvise` seguinte, e separá-los do
buffer que preenchem custaria mais do que a proximidade compra. A regra de conteúdo vale
igual nos dois arquivos — catch-all, nunca propaga, `S_OK` sempre.

Um estilo que falha derruba aquele elemento e nada mais: um tema com 50 regras onde 3
quebraram aplica 47.

### 7.2 Vazamento de handles

Cada elemento reportado fica registrado na camada de diagnóstico e precisa ser liberado com
`IXamlDiagnosticsTestHooks::UnregisterInstance`, senão o explorer vaza memória
continuamente. Essa interface é privada, obtida por QI com GUID cravado; se sumir numa versão
futura do Windows, os elementos vazam.

- O contador de handles vivos é métrica de primeira classe, exposta em "Diagnóstico" no menu
  da bandeja. Crescimento monotônico indica bug. **Não implementado no Plano 2** (fix round 2
  da Task 4): a travessia sob demanda da Task 5 libera cada handle que toca dentro da própria
  travessia (via `ReleaseHandle`), então não sobra nada vivo para contar depois que ela
  termina — o que o Plano 2 expõe (`ReleasedHandleCount()`) é um contador de handles já
  liberados, monotônico pelo motivo oposto (só confirma que a liberação está disparando, não
  consegue flagrar um handle que ninguém liberou). O contador de handles *vivos* de verdade só
  faz sentido a partir do Plano 3, o primeiro a manter elementos vivos entre um evento de
  mudança e outro em vez de liberá-los imediatamente após o uso.
- Se `IXamlDiagnosticsTestHooks` estiver indisponível, o TAP **avisa** em vez de vazar calado.

**Nota (Plano 2 → Plano 3):** liberar um handle de dentro de `OnVisualTreeChange`
(a assinatura `IVisualTreeServiceCallback2` / `AdviseVisualTreeChange` descrita no fluxo do
§4.1) não é seguro sem cuidado extra: o aviso chega de dentro do Leave walk do explorer, que
ainda está visitando a subárvore sendo removida, então liberar ali destrói o objeto no meio da
travessia. O upstream chama isso de "the one thing that isn't safe" e resolve enfileirando a
liberação e drenando a fila na thread do dispatcher do host
(`vendor/upstream/windows-11-taskbar-styler.wh.cpp:11168`, `:18379`, `:18404`). O Plano 2 assina, mas só por um instante: sua
Task 5 chama `AdviseVisualTreeChange`, recebe o lote inicial — que chega síncrono dentro da
própria chamada, medido em spike —, chama `UnadviseVisualTreeChange`, e só então formata e
libera cada handle, fora de qualquer callback. O callback do instantâneo copia valores e nada
mais: nunca libera handle. É por isso que o Plano 2 não precisa do dreno adiado. A assinatura
que **fica de pé** ao longo do tempo, e o dreno que ela exige para ser segura, ficam para o
Plano 3, que é o primeiro a precisar de notificação de mudança ao vivo para aplicar estilo
incrementalmente.

### 7.3 Log

`OnVisualTreeChange` dispara por elemento — centenas na abertura da taskbar.

| Nível | Grava | Custo |
|---|---|---|
| `Erro` | falha de carga, estilo não aplicado, vazamento | ~zero |
| `Info` (padrão) | carga do TAP, tema aplicado, hosts, reinício do explorer | desprezível |
| `Debug` | cada elemento da árvore | pesado, sob demanda |

`Info` é o padrão porque os eventos desse nível (carga do TAP, tema aplicado) disparam uma vez
por carregamento, não por elemento — o próprio custo "desprezível" acima já reflete isso. O
tráfego pesado é o de `Debug` (`OnVisualTreeChange`, por elemento), que continua desligado por
padrão; é ali que está o custo que esta tabela protege.

A checagem de nível ocorre **antes** de formatar a string. Arquivo em
`%LOCALAPPDATA%\TaskbarStyler\log.txt`, rotação em 1 MB, mais `OutputDebugStringW`.

### 7.4 Estados da bandeja

**Ativo** · **Inativo** · **Falhou** (tooltip com motivo e `HRESULT`, menu "Abrir log") ·
**Aguardando** (explorer reiniciando).

Nunca há diálogo modal. O TAP roda dentro do explorer; um `MessageBox` de lá trava o shell.

### 7.5 Exportar árvore visual

A falha provável no uso real não é crash: é o Windows atualizar, um elemento ser renomeado, e
o tema parar de funcionar. Sem ferramenta própria, a saída seria esperar terceiros — a
dependência que o projeto existe para eliminar.

"Exportar árvore visual" no menu da bandeja percorre a árvore e escreve tipo, nome e
hierarquia no mesmo formato dos seletores dos temas:

```
Taskbar.TaskbarFrame
  Grid#RootGrid
    Taskbar.TaskbarBackground
      Grid
        Rectangle#BackgroundFill
        Rectangle#BackgroundStroke
```

Travessia de ~50 linhas, já que `IVisualTreeService3` está disponível.

### 7.6 Falhar fechado

JSON malformado, seletor inválido ou regra de estilo inválida: o TAP **não aplica o tema** e
reporta. Nunca aplica pela metade — meia taskbar estilizada parece bug do Windows.

**Exceção deliberada: referência a constante inexistente não é erro.** O upstream substitui
`$Nome` por prefixo em qualquer posição do valor e deixa um `$` sem correspondência passar como
literal (`ApplyStyleConstants`, `vendor/upstream/...:18536`). Isso não é descuido: dos dados
reais, 85 das 1567 referências estão embutidas no meio do valor, e 10 não resolvem contra
constante alguma. Apenas o tema `Fluid` depende de fato do passthrough de um `$` sem
correspondência; `Luminosity_variant_Dock` e `Luminosity_variant_Compact` referenciam
`$WidgetGap57`, que a regra de prefixo mais longo do upstream (`LoadStyleConstants`,
`vendor/upstream/...:18580`) resolve como `WidgetGap` + o literal `57`, sem sobrar `$` algum.
Validar mais estrito que o upstream rejeitaria temas que
funcionam hoje. A resolução acontece na aplicação, não na carga.

**Duas exceções deliberadas por regra: falha aquela regra, não o tema — e é reportada.**
`AddElementCustomizationRules` (`vendor/upstream/...:18956`) do upstream envolve cada alvo em um
try/catch próprio: uma regra malformada é descartada individualmente, o tema inteiro continua
carregando. O core replica essa tolerância por regra — nunca por tema inteiro, nunca em silêncio:
a regra permanece em `Theme::rules` (a contagem de regras continua íntegra) e uma linha legível é
anexada a `Theme::diagnostics` nomeando o tema, o alvo e o motivo. Isso não implica
`ThemeRule::dead = true` em todo caso — uma cadeia de seletor descartada de um alvo com várias
(ver segunda exceção abaixo) gera diagnóstico mas deixa a regra viva, já que as cadeias restantes
ainda casam. Consumidores da árvore de regras **devem** checar `dead` antes de aplicar qualquer
regra, independentemente de ela ter gerado diagnóstico.

- *Uma entrada de estilo literalmente vazia (`""`) mata os estilos daquela regra.* Essa string
  exata — não "qualquer entrada malformada" — corresponde ao upstream lançando
  `"'=' is missing"` (`vendor/upstream/...:18727`); o catch por alvo descarta a customização
  inteira daquele alvo, inclusive estilos já interpretados antes do problemático. O core
  espelha isso: zera `ThemeRule::styles` por completo, não só a entrada vazia. Nos dados reais,
  os quatro `""` de `LiquidGlass2.json` são a única entrada de estilo da própria regra —
  coincidência dos dados, não da regra. **Qualquer outra entrada malformada (sem `=`, tipo
  errado, etc.) continua falhando o tema inteiro fechado**, exatamente como antes desta
  exceção existir — a tolerância aqui é estreita de propósito, não uma reinterpretação geral
  de "o que `ParseStyleRule` rejeitaria".
- *Uma cadeia de seletor inválida é descartada.* Um alvo separado por vírgulas vira múltiplas
  cadeias alternativas (`SplitTargetString`/`ParseSelectorGroups`); se uma cadeia específica é
  ambígua (dois `#Nome` colados sem `>` entre eles — um bug de autoria real do
  `LiquidGlass2.json`), só aquela cadeia cai, cadeias-irmãs do mesmo alvo continuam válidas. A
  regra só vira `dead` quando **todas** as suas cadeias falham, deixando `ThemeRule::selector`
  vazio (não casa com nada).

Isso espelha a tolerância por alvo do upstream; o que muda é que o core nunca fica calado a
respeito — onde o upstream apenas loga e segue, `Theme::diagnostics` torna o fato auditável pelo
chamador, cumprindo o "reporta" deste parágrafo em vez de deixá-lo implícito.

## 8. Testes

### 8.1 Automatizado (CI)

- **Parsers do core**, contra dois conjuntos: os 2.396 seletores e milhares de regras de estilo
  reais dos 55 temas (corpus escrito por dezenas de pessoas ao longo de anos), e casos de borda
  escritos à mão. As mensagens de erro do parser original são a lista de entradas inválidas a
  cobrir.
- **Round-trip do conversor** (§5.4).
- **Carga dos 55 JSONs**: zero erro, contagem de regras conferindo.
- **Máquina de estados do Tray**, extraída como classe pura em C#.

Os parsers são escritos **teste primeiro**: a gramática e seus casos de borda já estão
especificados no código do mod.

### 8.2 Manual, versionado em `docs/smoke-test.md`

- TAP carregar, aplicar e desfazer
- reinício do explorer, segundo monitor, flyouts
- contador de handles estável ao longo do tempo

Um runner de CI não tem taskbar interativa. Um teste que finge cobrir isso é pior que a
ausência dele, porque induz confiança no verde.

## 9. Repositório, licença e build

```
README.md
LICENSE                 GPL-3.0
NOTICE                  crédito a m417z e ao projeto original
THEMES.md               autor de cada tema
CMakeLists.txt
src/core/               styler_core — parsers, sem Windows
src/tap/                a DLL; tap_boundary.cpp isola as entradas COM
src/tray/               C# .NET 10
themes/                 55 arquivos JSON (54 selecionáveis + a variante do Squircle)
tools/extract_themes.py  conversor + round-trip
tests/core/             testes unitários
docs/smoke-test.md      checklist manual
.github/workflows/ci.yml
```

Build: CMake + Ninja (ambos já incluídos no Visual Studio) para C++, `dotnet build` para C#.
CI em `windows-latest`: compila os dois, roda testes do core e round-trip do conversor.

### 9.1 Licença

O mod é GPLv3; este projeto é obra derivada e portanto **GPLv3**.

- `NOTICE` credita m417z e o projeto original, com link.
- `THEMES.md` credita cada autor de tema, extraído do readme upstream.
- Cada JSON de tema carrega `"author"`.

Repositório público, o que já satisfaz a obrigação de disponibilizar o fonte.

## 10. Riscos conhecidos

| Risco | Severidade | Mitigação |
|---|---|---|
| `InitializeXamlDiagnosticsEx` mudar numa atualização do Windows | alta | Sem mitigação real; a API é não documentada. Falha detectada e reportada, não silenciosa. |
| `IXamlDiagnosticsTestHooks` sumir | média | Detectado no `SetSite`; avisa em vez de vazar calado. |
| Renomeação de elementos da taskbar pela Microsoft quebrar temas | alta, recorrente | "Exportar árvore visual" (§7.5) permite ao usuário corrigir sozinho. |
| Antivírus sinalizar o app | baixa | Nenhuma API de injeção nem patch de código é usada (§3, §6.2). |
| Conversão de temas introduzir erro sutil | média | Round-trip com diff (§5.4). |

## 11. Critérios de sucesso

1. Aplicar qualquer um dos 54 temas e obter resultado visualmente idêntico ao do mod no
   Windhawk.
2. Sobreviver a reinício do explorer sem intervenção.
3. Aplicar em taskbar de monitor secundário conectado após o início.
4. Desativar e restaurar a taskbar ao estado original.
5. Contador de handles estável ao longo de uma sessão de 24h.
6. Zero requisições de rede em toda a operação.
