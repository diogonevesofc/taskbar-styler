# Spike: travessia por pull (COM puro) vs assinatura do stream de mutacoes

Branch: `spike-pull-walk` (codigo descartavel; **nao** esta em `plano-2-tap-arvore-visual`).

Ambiente: Windows 11 Pro 10.0.26200, host explorer.exe, TAP carregado por
`taskbar-styler load` (VisualDiagConnection1). Toda a sonda roda de dentro do
`SetSite`, logo depois de `OpenDiagnostics`, dentro de um `catch (...)`, e
solta todo handle por `ReleaseHandle` e toda interface por `Release`.
Arquivo da sonda: `src/tap/spike_pull_walk.cpp` (chamado de `tap_boundary.cpp`).

Resumo de uma linha por pergunta:

- **Q1 - NAO** (para a barra de tarefas). `GetUiLayer` e `GetApplication` retornam `S_OK`,
  mas nenhum dos dois esta na arvore da taskbar; `HitTest` retorna `E_INVALIDARG`
  em todos os retangulos; `Window.Current.Content` e nulo (host de ilha XAML).
- **Q2 - SIM, com duas ressalvas.** `GetPropertyValuesChain` + `GetCollectionCount` +
  `GetCollectionElements` devolvem handles de filhos de verdade - desde que
  `pElementCount` seja passado como **in/out** - mas controles com `Template`
  (ex.: `Taskbar.TaskbarFrame`) nao expoem o filho visual em propriedade nenhuma.
- **Q3 - NAO pelo `GetUiLayer`.** O UI layer e um `Grid` solto: `Parent` nulo e
  `Children` vazio. Elementos reais tem `Parent` utilizavel, mas so se chega a um
  elemento real pelo stream de mutacoes.

---

## Q1 - da para obter um handle vivo da taskbar SEM assinar?

Tecnica do upstream primeiro (QI de `IInspectable` e valor do ponteiro,
`vendor/upstream/windows-11-taskbar-styler.wh.cpp:10954`), e so depois
`GetHandleFromIInspectable`.

```
[01:01:11.341 8836/26084] INFO SetSite(000000001C304B80)
[01:01:11.342 8836/26084] INFO loaded into C:\WINDOWS\explorer.exe
[01:01:11.342 8836/26084] INFO diagnostics session open
[01:01:11.342 8836/26084] INFO [spike] QI IVisualTreeService3 hr=0x00000000
[01:01:11.343 8836/26084] INFO [spike] Q1 GetUiLayer hr=0x00000000 ptr=000000001BAF6E30
[01:01:11.343 8836/26084] INFO [spike] Q1 GetApplication hr=0x00000000 ptr=0000000019AE36A8
[01:01:11.344 8836/26084] INFO [spike] Q1 UiLayer: runtime class [Windows.UI.Xaml.Controls.Grid], handle(from pointer)=464481848
[01:01:11.344 8836/26084] INFO [spike] Q1 UiLayer: GetIInspectableFromHandle(464481848) hr=0x80070490
[01:01:11.344 8836/26084] INFO [spike] GetPropertyValuesChain(pointer-handle handle=464481848) hr=0x80070490 sources=0 values=0
[01:01:11.345 8836/26084] INFO [spike] Q1 UiLayer: pointer-derived handle NOT usable; trying GetHandleFromIInspectable
[01:01:11.345 8836/26084] INFO [spike] Q1 UiLayer: GetHandleFromIInspectable hr=0x00000000 handle=464481848 (same value as pointer handle: 1)
[01:01:11.348 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=464481848) hr=0x00000000 sources=2 values=234
```

O valor numerico derivado do ponteiro esta **certo** (`GetHandleFromIInspectable`
devolve exatamente o mesmo numero: `same value as pointer handle: 1`), mas ele
nao serve para a API de pull enquanto o diagnostics nao o tiver registrado:
`GetIInspectableFromHandle` e `GetPropertyValuesChain` devolvem `0x80070490`
(`HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`). Ou seja: a tecnica do upstream **nao**
basta aqui - ela existe para nomear elementos que o proprio callback ja
reportou (e que por isso ja estao no cache), nao para descobrir elementos novos.
Depois de `GetHandleFromIInspectable` a cadeia funciona (234 propriedades), e o
handle e devolvido com `ReleaseHandle`/`UnregisterInstance` no fim.

O que esse handle e, porem:

```
[01:01:11.428 8836/26084] INFO [spike] DOWN d=0 Windows.UI.Xaml.Controls.Grid Name=[] handle=464481848
[01:01:11.428 8836/26084] INFO [spike] [Children] -> handle=471520024 collection=1
[01:01:11.428 8836/26084] INFO [spike]   GetCollectionCount(471520024) hr=0x00000000 count=0
[01:01:11.436 8836/26084] INFO [spike] UP level=0 Windows.UI.Xaml.Controls.Grid handle=464481848 parent=0
[01:01:11.436 8836/26084] INFO [spike] Q1 Application: runtime class [Windows.Internal.Shell.XamlExplorerHost.XamlApplication], handle(from pointer)=428198200
[01:01:11.437 8836/26084] INFO [spike] Q1 Application: GetIInspectableFromHandle(428198200) hr=0x80070490
[01:01:11.437 8836/26084] INFO [spike] GetPropertyValuesChain(pointer-handle handle=428198200) hr=0x80070490 sources=0 values=0
[01:01:11.437 8836/26084] INFO [spike] Q1 Application: pointer-derived handle NOT usable; trying GetHandleFromIInspectable
[01:01:11.438 8836/26084] INFO [spike] Q1 Application: GetHandleFromIInspectable hr=0x00000000 handle=428198200 (same value as pointer handle: 1)
[01:01:11.439 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=428198200) hr=0x80070490 sources=0 values=0
[01:01:11.439 8836/26084] INFO [spike] GetPropertyValuesChain(up handle=428198200) hr=0x80070490 sources=0 values=0
```

(as 234 linhas `prop[...]` do UI layer foram omitidas aqui; o dump completo de
um elemento real esta em Q2)

`GetUiLayer` = um `Windows.UI.Xaml.Controls.Grid` com `Children` de contagem 0 e
`Parent` nulo - a camada de adornos do diagnostics, nao a taskbar.
`GetApplication` = `Windows.Internal.Shell.XamlExplorerHost.XamlApplication`;
`GetHandleFromIInspectable` aceita, mas o visual tree service nao conhece o
handle (`0x80070490`) - nao e elemento de arvore.

As outras duas portas sem assinatura, testadas no mesmo run:

```
[01:01:11.439 8836/26084] INFO [spike] Q3 Shell_TrayWnd rect 0,1032 1920,1080
[01:01:11.440 8836/26084] INFO [spike] Q3 HitTest(0,1032,1920,1080) hr=0x80070057 count=0
[01:01:11.440 8836/26084] INFO [spike] Q3 HitTest(960,1056,964,1060) hr=0x80070057 count=0
[01:01:11.441 8836/26084] INFO [spike] Q3 HitTest(0,0,1920,48) hr=0x80070057 count=0
[01:01:11.441 8836/26084] INFO [spike] Q3 HitTest(0,0,1920,48) hr=0x80070057 count=0
[01:01:11.441 8836/26084] INFO [spike] Q3 HitTest(0,0,1920,1080) hr=0x80070057 count=0
[01:01:11.442 8836/26084] INFO [spike] Q3 HitTest(0,0,1,1) hr=0x80070057 count=0
[01:01:11.442 8836/26084] INFO [spike] Q3 RoGetActivationFactory(Window) hr=0x00000000
[01:01:11.442 8836/26084] INFO [spike] Q3 Window.Current hr=0x00000000 ptr=00000000198BE388
[01:01:11.443 8836/26084] INFO [spike] Q3 Window.Content hr=0x00000000 ptr=0000000000000000
```

`HitTest` devolve `E_INVALIDARG` (0x80070057) em **todos** os espacos de
coordenada testados (tela inteira, retangulo do `Shell_TrayWnd`, 4x4 no centro,
cliente, cliente em DIPs, 1x1). `Window.Current` existe mas `Content` e nulo -
a taskbar e hospedada por `DesktopWindowXamlSource` (ilha), nao por um
`CoreWindow`.

**Conclusao Q1: nao ha porta sem assinatura que chegue a arvore viva da taskbar.**

---

## Q2 - da para enumerar filhos so com a API COM?

Como Q1 falhou, a unica forma de ter um handle de elemento **real** da taskbar
foi assinar. Isto aqui e *medicao*, nao escolha de desenho: a sonda chama
`AdviseVisualTreeChange`, guarda os handles da enxurrada inicial sem tocar em
nada dentro do callback, chama `UnadviseVisualTreeChange` em seguida e so
**depois** - fora de qualquer callback, fora do Leave walk do explorer - usa a
API de pull. Assim a pergunta [o pull anda na arvore?] fica separada da
pergunta [como se pega o primeiro handle?].

```
[01:01:11.449 8836/26084] INFO [spike] Q2 AdviseVisualTreeChange hr=0x00000000 reported=200
[01:01:11.450 8836/26084] INFO [spike] Q2 UnadviseVisualTreeChange hr=0x00000000
[01:01:11.461 8836/26084] INFO [spike] Q2 pull-walking Windows.UI.Xaml.Internal.RootScrollViewer handle=55888840 (children=1)
[01:01:11.462 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=55888840) hr=0x00000000 sources=2 values=299
```

Dump completo (verbatim) do primeiro elemento real, `RootScrollViewer`
(handle 55888840) - 299 propriedades:

```
[01:01:11.463 8836/26084] INFO [spike]   source[0] handle=55888840 TargetType=[] Name=[] Source=4
[01:01:11.463 8836/26084] INFO [spike]   source[1] handle=0 TargetType=[] Name=[] Source=1
[01:01:11.463 8836/26084] INFO [spike]   prop[0] AutomationProperties.AcceleratorKey : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.464 8836/26084] INFO [spike]   prop[1] AutomationProperties.AccessibilityView : Type=Windows.UI.Xaml.Automation.Peers.AccessibilityView ValueType=Windows.UI.Xaml.Automation.Peers.AccessibilityView ItemType=(null) bits=0x0 chainIdx=1 value=2
[01:01:11.464 8836/26084] INFO [spike]   prop[2] AutomationProperties.AccessKey : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.464 8836/26084] INFO [spike]   prop[3] AutomationProperties.Annotations : Type=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ValueType=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ItemType=(null) bits=0x5 chainIdx=1 value=469407576
[01:01:11.464 8836/26084] INFO [spike]   prop[4] AutomationProperties.AutomationControlType : Type=Windows.UI.Xaml.Automation.Peers.AutomationControlType ValueType=Windows.UI.Xaml.Automation.Peers.AutomationControlType ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.466 8836/26084] INFO [spike]   prop[5] AutomationProperties.AutomationId : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.466 8836/26084] INFO [spike]   prop[6] AutomationProperties.ControlledPeers : Type=Windows.UI.Xaml.Controls.UIElementCollection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x22 chainIdx=1 value=0
[01:01:11.466 8836/26084] INFO [spike]   prop[7] AutomationProperties.Culture : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=1046
[01:01:11.467 8836/26084] INFO [spike]   prop[8] AutomationProperties.DescribedBy : Type=Windows.UI.Xaml.DependencyObjectCollection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.467 8836/26084] INFO [spike]   prop[9] AutomationProperties.FlowsFrom : Type=Windows.UI.Xaml.DependencyObjectCollection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.467 8836/26084] INFO [spike]   prop[10] AutomationProperties.FlowsTo : Type=Windows.UI.Xaml.DependencyObjectCollection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.468 8836/26084] INFO [spike]   prop[11] AutomationProperties.FullDescription : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.468 8836/26084] INFO [spike]   prop[12] AutomationProperties.HeadingLevel : Type=Windows.UI.Xaml.Automation.Peers.AutomationHeadingLevel ValueType=Windows.UI.Xaml.Automation.Peers.AutomationHeadingLevel ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.468 8836/26084] INFO [spike]   prop[13] AutomationProperties.HelpText : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.469 8836/26084] INFO [spike]   prop[14] AutomationProperties.IsDataValidForForm : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.469 8836/26084] INFO [spike]   prop[15] AutomationProperties.IsDialog : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.469 8836/26084] INFO [spike]   prop[16] AutomationProperties.IsPeripheral : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.470 8836/26084] INFO [spike]   prop[17] AutomationProperties.IsRequiredForForm : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.470 8836/26084] INFO [spike]   prop[18] AutomationProperties.ItemStatus : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.470 8836/26084] INFO [spike]   prop[19] AutomationProperties.ItemType : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.470 8836/26084] INFO [spike]   prop[20] AutomationProperties.LabeledBy : Type=Windows.UI.Xaml.UIElement ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.471 8836/26084] INFO [spike]   prop[21] AutomationProperties.LandmarkType : Type=Windows.UI.Xaml.Automation.Peers.AutomationLandmarkType ValueType=Windows.UI.Xaml.Automation.Peers.AutomationLandmarkType ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.471 8836/26084] INFO [spike]   prop[22] AutomationProperties.Level : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=-1
[01:01:11.471 8836/26084] INFO [spike]   prop[23] AutomationProperties.LiveSetting : Type=Windows.UI.Xaml.Automation.Peers.AutomationLiveSetting ValueType=Windows.UI.Xaml.Automation.Peers.AutomationLiveSetting ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.472 8836/26084] INFO [spike]   prop[24] AutomationProperties.LocalizedControlType : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.472 8836/26084] INFO [spike]   prop[25] AutomationProperties.LocalizedLandmarkType : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.472 8836/26084] INFO [spike]   prop[26] AutomationProperties.Name : Type=Windows.Foundation.String ValueType=Windows.Foundation.String ItemType=(null) bits=0x0 chainIdx=1 value=
[01:01:11.473 8836/26084] INFO [spike]   prop[27] AutomationProperties.PositionInSet : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=-1
[01:01:11.473 8836/26084] INFO [spike]   prop[28] AutomationProperties.SizeOfSet : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=-1
[01:01:11.473 8836/26084] INFO [spike]   prop[29] MediaTransportControlsHelper.DropoutOrder : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.474 8836/26084] INFO [spike]   prop[30] ToolTipService.Placement : Type=Windows.UI.Xaml.Controls.Primitives.PlacementMode ValueType=Windows.UI.Xaml.Controls.Primitives.PlacementMode ItemType=(null) bits=0x0 chainIdx=1 value=10
[01:01:11.474 8836/26084] INFO [spike]   prop[31] ToolTipService.PlacementTarget : Type=Windows.UI.Xaml.UIElement ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.474 8836/26084] INFO [spike]   prop[32] ToolTipService.ToolTip : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.474 8836/26084] INFO [spike]   prop[33] Typography.AnnotationAlternates : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.475 8836/26084] INFO [spike]   prop[34] Typography.Capitals : Type=Windows.UI.Xaml.FontCapitals ValueType=Windows.UI.Xaml.FontCapitals ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.475 8836/26084] INFO [spike]   prop[35] Typography.CapitalSpacing : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.475 8836/26084] INFO [spike]   prop[36] Typography.CaseSensitiveForms : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.476 8836/26084] INFO [spike]   prop[37] Typography.ContextualAlternates : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.476 8836/26084] INFO [spike]   prop[38] Typography.ContextualLigatures : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.476 8836/26084] INFO [spike]   prop[39] Typography.ContextualSwashes : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.477 8836/26084] INFO [spike]   prop[40] Typography.DiscretionaryLigatures : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.477 8836/26084] INFO [spike]   prop[41] Typography.EastAsianExpertForms : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.477 8836/26084] INFO [spike]   prop[42] Typography.EastAsianLanguage : Type=Windows.UI.Xaml.FontEastAsianLanguage ValueType=Windows.UI.Xaml.FontEastAsianLanguage ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.478 8836/26084] INFO [spike]   prop[43] Typography.EastAsianWidths : Type=Windows.UI.Xaml.FontEastAsianWidths ValueType=Windows.UI.Xaml.FontEastAsianWidths ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.478 8836/26084] INFO [spike]   prop[44] Typography.Fraction : Type=Windows.UI.Xaml.FontFraction ValueType=Windows.UI.Xaml.FontFraction ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.478 8836/26084] INFO [spike]   prop[45] Typography.HistoricalForms : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.479 8836/26084] INFO [spike]   prop[46] Typography.HistoricalLigatures : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.479 8836/26084] INFO [spike]   prop[47] Typography.Kerning : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.479 8836/26084] INFO [spike]   prop[48] Typography.MathematicalGreek : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.480 8836/26084] INFO [spike]   prop[49] Typography.NumeralAlignment : Type=Windows.UI.Xaml.FontNumeralAlignment ValueType=Windows.UI.Xaml.FontNumeralAlignment ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.480 8836/26084] INFO [spike]   prop[50] Typography.NumeralStyle : Type=Windows.UI.Xaml.FontNumeralStyle ValueType=Windows.UI.Xaml.FontNumeralStyle ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.480 8836/26084] INFO [spike]   prop[51] Typography.SlashedZero : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.480 8836/26084] INFO [spike]   prop[52] Typography.StandardLigatures : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.481 8836/26084] INFO [spike]   prop[53] Typography.StandardSwashes : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.481 8836/26084] INFO [spike]   prop[54] Typography.StylisticAlternates : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.481 8836/26084] INFO [spike]   prop[55] Typography.StylisticSet1 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.482 8836/26084] INFO [spike]   prop[56] Typography.StylisticSet10 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.482 8836/26084] INFO [spike]   prop[57] Typography.StylisticSet11 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.482 8836/26084] INFO [spike]   prop[58] Typography.StylisticSet12 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.483 8836/26084] INFO [spike]   prop[59] Typography.StylisticSet13 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.483 8836/26084] INFO [spike]   prop[60] Typography.StylisticSet14 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.483 8836/26084] INFO [spike]   prop[61] Typography.StylisticSet15 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.484 8836/26084] INFO [spike]   prop[62] Typography.StylisticSet16 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.484 8836/26084] INFO [spike]   prop[63] Typography.StylisticSet17 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.484 8836/26084] INFO [spike]   prop[64] Typography.StylisticSet18 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.484 8836/26084] INFO [spike]   prop[65] Typography.StylisticSet19 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.485 8836/26084] INFO [spike]   prop[66] Typography.StylisticSet2 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.485 8836/26084] INFO [spike]   prop[67] Typography.StylisticSet20 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.485 8836/26084] INFO [spike]   prop[68] Typography.StylisticSet3 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.486 8836/26084] INFO [spike]   prop[69] Typography.StylisticSet4 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.486 8836/26084] INFO [spike]   prop[70] Typography.StylisticSet5 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.486 8836/26084] INFO [spike]   prop[71] Typography.StylisticSet6 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.487 8836/26084] INFO [spike]   prop[72] Typography.StylisticSet7 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.487 8836/26084] INFO [spike]   prop[73] Typography.StylisticSet8 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.487 8836/26084] INFO [spike]   prop[74] Typography.StylisticSet9 : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.487 8836/26084] INFO [spike]   prop[75] Typography.Variants : Type=Windows.UI.Xaml.FontVariants ValueType=Windows.UI.Xaml.FontVariants ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.488 8836/26084] INFO [spike]   prop[76] XamlBindingHelper.DataTemplateComponent : Type=Windows.UI.Xaml.Markup.IDataTemplateComponent ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.488 8836/26084] INFO [spike]   prop[77] FlyoutBase.AttachedFlyout : Type=Windows.UI.Xaml.Controls.Primitives.FlyoutBase ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.488 8836/26084] INFO [spike]   prop[78] AccessKey : Type=Windows.Foundation.String ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.489 8836/26084] INFO [spike]   prop[79] AccessKeyScopeOwner : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.489 8836/26084] INFO [spike]   prop[80] ActualOffset : Type=Windows.Foundation.Numerics.Vector3 ValueType=Windows.Foundation.Object ItemType=(null) bits=0x22 chainIdx=1 value=0
[01:01:11.489 8836/26084] INFO [spike]   prop[81] ActualSize : Type=Windows.Foundation.Numerics.Vector2 ValueType=Windows.Foundation.Object ItemType=(null) bits=0x22 chainIdx=1 value=0
[01:01:11.490 8836/26084] INFO [spike]   prop[82] AllowDrop : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.490 8836/26084] INFO [spike]   prop[83] CacheMode : Type=Windows.UI.Xaml.Media.CacheMode ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.490 8836/26084] INFO [spike]   prop[84] CanBeScrollAnchor : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.491 8836/26084] INFO [spike]   prop[85] CanDrag : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.491 8836/26084] INFO [spike]   prop[86] Clip : Type=Windows.UI.Xaml.Media.Geometry ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.491 8836/26084] INFO [spike]   prop[87] CompositeMode : Type=Windows.UI.Xaml.Media.ElementCompositeMode ValueType=Windows.UI.Xaml.Media.ElementCompositeMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.491 8836/26084] INFO [spike]   prop[88] ContextFlyout : Type=Windows.UI.Xaml.Controls.Primitives.FlyoutBase ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.492 8836/26084] INFO [spike]   prop[89] ExitDisplayModeOnAccessKeyInvoked : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.492 8836/26084] INFO [spike]   prop[90] HighContrastAdjustment : Type=Windows.UI.Xaml.ElementHighContrastAdjustment ValueType=Windows.UI.Xaml.ElementHighContrastAdjustment ItemType=(null) bits=0x0 chainIdx=1 value=-2147483648
[01:01:11.492 8836/26084] INFO [spike]   prop[91] IsAccessKeyScope : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.493 8836/26084] INFO [spike]   prop[92] IsDoubleTapEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.493 8836/26084] INFO [spike]   prop[93] IsHitTestVisible : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.493 8836/26084] INFO [spike]   prop[94] IsHoldingEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.494 8836/26084] INFO [spike]   prop[95] IsRightTapEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.494 8836/26084] INFO [spike]   prop[96] IsTapEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.495 8836/26084] INFO [spike]   prop[97] KeyboardAcceleratorPlacementMode : Type=Windows.UI.Xaml.Input.KeyboardAcceleratorPlacementMode ValueType=Windows.UI.Xaml.Input.KeyboardAcceleratorPlacementMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.495 8836/26084] INFO [spike]   prop[98] KeyboardAcceleratorPlacementTarget : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.495 8836/26084] INFO [spike]   prop[99] KeyboardAccelerators : Type=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ValueType=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ItemType=(null) bits=0x5 chainIdx=1 value=469402056
[01:01:11.496 8836/26084] INFO [spike]   prop[100] KeyTipHorizontalOffset : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.496 8836/26084] INFO [spike]   prop[101] KeyTipPlacementMode : Type=Windows.UI.Xaml.Input.KeyTipPlacementMode ValueType=Windows.UI.Xaml.Input.KeyTipPlacementMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.496 8836/26084] INFO [spike]   prop[102] KeyTipTarget : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.496 8836/26084] INFO [spike]   prop[103] KeyTipVerticalOffset : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.498 8836/26084] INFO [spike]   prop[104] Lights : Type=Windows.UI.Xaml.Media.XamlLightCollection ValueType=Windows.UI.Xaml.Media.XamlLightCollection ItemType=(null) bits=0x7 chainIdx=1 value=428368520
[01:01:11.498 8836/26084] INFO [spike]   prop[105] ManipulationMode : Type=Windows.UI.Xaml.Input.ManipulationModes ValueType=Windows.UI.Xaml.Input.ManipulationModes ItemType=(null) bits=0x0 chainIdx=1 value=65536
[01:01:11.499 8836/26084] INFO [spike]   prop[106] Opacity : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.499 8836/26084] INFO [spike]   prop[107] OpacityTransition : Type=Windows.UI.Xaml.ScalarTransition ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.500 8836/26084] INFO [spike]   prop[108] PointerCaptures : Type=Windows.UI.Xaml.Input.PointerCollection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x22 chainIdx=1 value=0
[01:01:11.500 8836/26084] INFO [spike]   prop[109] Projection : Type=Windows.UI.Xaml.Media.Projection ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.500 8836/26084] INFO [spike]   prop[110] RenderSize : Type=Windows.Foundation.Size ValueType=Windows.Foundation.Size ItemType=(null) bits=0x2 chainIdx=1 value=1920x48
[01:01:11.500 8836/26084] INFO [spike]   prop[111] RenderTransform : Type=Windows.UI.Xaml.Media.Transform ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.501 8836/26084] INFO [spike]   prop[112] RenderTransformOrigin : Type=Windows.Foundation.Point ValueType=Windows.Foundation.Point ItemType=(null) bits=0x0 chainIdx=1 value=0,0
[01:01:11.501 8836/26084] INFO [spike]   prop[113] RotationTransition : Type=Windows.UI.Xaml.ScalarTransition ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.501 8836/26084] INFO [spike]   prop[114] ScaleTransition : Type=Windows.UI.Xaml.Vector3Transition ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.502 8836/26084] INFO [spike]   prop[115] Shadow : Type=Windows.UI.Xaml.Media.Shadow ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.502 8836/26084] INFO [spike]   prop[116] TabFocusNavigation : Type=Windows.UI.Xaml.Input.KeyboardNavigationMode ValueType=Windows.UI.Xaml.Input.KeyboardNavigationMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.502 8836/26084] INFO [spike]   prop[117] Transform3D : Type=Windows.UI.Xaml.Media.Media3D.Transform3D ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.502 8836/26084] INFO [spike]   prop[118] Transitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=464580088
[01:01:11.503 8836/26084] INFO [spike]   prop[119] TranslationTransition : Type=Windows.UI.Xaml.Vector3Transition ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.503 8836/26084] INFO [spike]   prop[120] UseLayoutRounding : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.503 8836/26084] INFO [spike]   prop[121] Visibility : Type=Windows.UI.Xaml.Visibility ValueType=Windows.UI.Xaml.Visibility ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.504 8836/26084] INFO [spike]   prop[122] XYFocusDownNavigationStrategy : Type=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ValueType=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.504 8836/26084] INFO [spike]   prop[123] XYFocusKeyboardNavigation : Type=Windows.UI.Xaml.Input.XYFocusKeyboardNavigationMode ValueType=Windows.UI.Xaml.Input.XYFocusKeyboardNavigationMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.504 8836/26084] INFO [spike]   prop[124] XYFocusLeftNavigationStrategy : Type=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ValueType=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.505 8836/26084] INFO [spike]   prop[125] XYFocusRightNavigationStrategy : Type=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ValueType=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.505 8836/26084] INFO [spike]   prop[126] XYFocusUpNavigationStrategy : Type=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ValueType=Windows.UI.Xaml.Input.XYFocusNavigationStrategy ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.505 8836/26084] INFO [spike]   prop[127] VisualStateManager.CustomVisualStateManager : Type=Windows.UI.Xaml.VisualStateManager ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.506 8836/26084] INFO [spike]   prop[128] VisualStateManager.VisualStateGroups : Type=Windows.UI.Xaml.Internal.VisualStateGroupCollection ValueType=Windows.UI.Xaml.Internal.VisualStateGroupCollection ItemType=(null) bits=0x7 chainIdx=1 value=474089928
[01:01:11.506 8836/26084] INFO [spike]   prop[129] DataTemplate.ExtensionInstance : Type=Windows.UI.Xaml.IDataTemplateExtension ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.506 8836/26084] INFO [spike]   prop[130] ActualHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=48
[01:01:11.507 8836/26084] INFO [spike]   prop[131] ActualTheme : Type=Windows.UI.Xaml.ElementTheme ValueType=Windows.UI.Xaml.ElementTheme ItemType=(null) bits=0x2 chainIdx=1 value=2
[01:01:11.507 8836/26084] INFO [spike]   prop[132] ActualWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=1920
[01:01:11.507 8836/26084] INFO [spike]   prop[133] AllowFocusOnInteraction : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.507 8836/26084] INFO [spike]   prop[134] AllowFocusWhenDisabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.508 8836/26084] INFO [spike]   prop[135] DataContext : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.508 8836/26084] INFO [spike]   prop[136] FlowDirection : Type=Windows.UI.Xaml.FlowDirection ValueType=Windows.UI.Xaml.FlowDirection ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.508 8836/26084] INFO [spike]   prop[137] FocusVisualMargin : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=0,0,0,0
[01:01:11.509 8836/26084] INFO [spike]   prop[138] FocusVisualPrimaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=466975176
[01:01:11.509 8836/26084] INFO [spike]   prop[139] FocusVisualPrimaryThickness : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=2,2,2,2
[01:01:11.509 8836/26084] INFO [spike]   prop[140] FocusVisualSecondaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=468613640
[01:01:11.510 8836/26084] INFO [spike]   prop[141] FocusVisualSecondaryThickness : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=1,1,1,1
[01:01:11.510 8836/26084] INFO [spike]   prop[142] Height : Type=Windows.Foundation.Double ValueType=Windows.UI.Xaml.GridLength ItemType=(null) bits=0x0 chainIdx=0 value=48
[01:01:11.510 8836/26084] INFO [spike]   prop[143] Height : Type=Windows.Foundation.Double ValueType=Windows.UI.Xaml.GridLength ItemType=(null) bits=0x0 chainIdx=1 value=Auto
[01:01:11.511 8836/26084] INFO [spike]   prop[144] HorizontalAlignment : Type=Windows.UI.Xaml.HorizontalAlignment ValueType=Windows.UI.Xaml.HorizontalAlignment ItemType=(null) bits=0x0 chainIdx=1 value=3
[01:01:11.511 8836/26084] INFO [spike]   prop[145] Language : Type=Windows.Foundation.String ValueType=Windows.Foundation.String ItemType=(null) bits=0x0 chainIdx=1 value=pt-BR
[01:01:11.511 8836/26084] INFO [spike]   prop[146] Margin : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=0,0,0,0
[01:01:11.512 8836/26084] INFO [spike]   prop[147] MaxHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=1.#INF
[01:01:11.512 8836/26084] INFO [spike]   prop[148] MaxWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=1.#INF
[01:01:11.512 8836/26084] INFO [spike]   prop[149] MinHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.512 8836/26084] INFO [spike]   prop[150] MinWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.513 8836/26084] INFO [spike]   prop[151] Parent : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.UI.Xaml.Hosting.XamlIsland ItemType=(null) bits=0x3 chainIdx=1 value=463155080
[01:01:11.513 8836/26084] INFO [spike]   prop[152] RequestedTheme : Type=Windows.UI.Xaml.ElementTheme ValueType=Windows.UI.Xaml.ElementTheme ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.513 8836/26084] INFO [spike]   prop[153] Style : Type=Windows.UI.Xaml.Style ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.514 8836/26084] INFO [spike]   prop[154] Tag : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.514 8836/26084] INFO [spike]   prop[155] Triggers : Type=Windows.UI.Xaml.TriggerCollection ValueType=Windows.UI.Xaml.TriggerCollection ItemType=(null) bits=0x5 chainIdx=1 value=470424792
[01:01:11.514 8836/26084] INFO [spike]   prop[156] VerticalAlignment : Type=Windows.UI.Xaml.VerticalAlignment ValueType=Windows.UI.Xaml.VerticalAlignment ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.515 8836/26084] INFO [spike]   prop[157] VerticalAlignment : Type=Windows.UI.Xaml.VerticalAlignment ValueType=Windows.UI.Xaml.VerticalAlignment ItemType=(null) bits=0x0 chainIdx=1 value=3
[01:01:11.515 8836/26084] INFO [spike]   prop[158] Width : Type=Windows.Foundation.Double ValueType=Windows.UI.Xaml.GridLength ItemType=(null) bits=0x0 chainIdx=0 value=1920
[01:01:11.515 8836/26084] INFO [spike]   prop[159] Width : Type=Windows.Foundation.Double ValueType=Windows.UI.Xaml.GridLength ItemType=(null) bits=0x0 chainIdx=1 value=Auto
[01:01:11.515 8836/26084] INFO [spike]   prop[160] Background : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.516 8836/26084] INFO [spike]   prop[161] BackgroundSizing : Type=Windows.UI.Xaml.Controls.BackgroundSizing ValueType=Windows.UI.Xaml.Controls.BackgroundSizing ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.516 8836/26084] INFO [spike]   prop[162] BorderBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.516 8836/26084] INFO [spike]   prop[163] BorderThickness : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=0,0,0,0
[01:01:11.517 8836/26084] INFO [spike]   prop[164] CharacterSpacing : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.517 8836/26084] INFO [spike]   prop[165] CornerRadius : Type=Windows.UI.Xaml.CornerRadius ValueType=Windows.UI.Xaml.CornerRadius ItemType=(null) bits=0x0 chainIdx=1 value=
[01:01:11.517 8836/26084] INFO [spike]   prop[166] DefaultStyleKey : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.UI.Xaml.Interop.TypeName ItemType=(null) bits=0x0 chainIdx=1 value=Windows.UI.Xaml.Internal.RootScrollViewer
[01:01:11.517 8836/26084] INFO [spike]   prop[167] DefaultStyleResourceUri : Type=Windows.Foundation.Uri ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.519 8836/26084] INFO [spike]   prop[168] ElementSoundMode : Type=Windows.UI.Xaml.ElementSoundMode ValueType=Windows.UI.Xaml.ElementSoundMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.519 8836/26084] INFO [spike]   prop[169] FocusState : Type=Windows.UI.Xaml.FocusState ValueType=Windows.UI.Xaml.FocusState ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.519 8836/26084] INFO [spike]   prop[170] FontFamily : Type=Windows.UI.Xaml.Media.FontFamily ValueType=Windows.UI.Xaml.Media.FontFamily ItemType=(null) bits=0x0 chainIdx=1 value=Segoe UI Variable
[01:01:11.520 8836/26084] INFO [spike]   prop[171] FontSize : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=14
[01:01:11.520 8836/26084] INFO [spike]   prop[172] FontStretch : Type=Windows.UI.Text.FontStretch ValueType=Windows.UI.Text.FontStretch ItemType=(null) bits=0x0 chainIdx=1 value=5
[01:01:11.520 8836/26084] INFO [spike]   prop[173] FontStyle : Type=Windows.UI.Text.FontStyle ValueType=Windows.UI.Text.FontStyle ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.521 8836/26084] INFO [spike]   prop[174] FontWeight : Type=Windows.UI.Text.FontWeight ValueType=Windows.UI.Text.FontWeight ItemType=(null) bits=0x0 chainIdx=1 value=Normal
[01:01:11.521 8836/26084] INFO [spike]   prop[175] Foreground : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=430817912
[01:01:11.521 8836/26084] INFO [spike]   prop[176] HorizontalContentAlignment : Type=Windows.UI.Xaml.HorizontalAlignment ValueType=Windows.UI.Xaml.HorizontalAlignment ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.522 8836/26084] INFO [spike]   prop[177] HorizontalContentAlignment : Type=Windows.UI.Xaml.HorizontalAlignment ValueType=Windows.UI.Xaml.HorizontalAlignment ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.522 8836/26084] INFO [spike]   prop[178] IsEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.522 8836/26084] INFO [spike]   prop[179] IsFocusEngaged : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.522 8836/26084] INFO [spike]   prop[180] IsFocusEngagementEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.523 8836/26084] INFO [spike]   prop[181] IsTabStop : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=0 value=1
[01:01:11.523 8836/26084] INFO [spike]   prop[182] IsTabStop : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.523 8836/26084] INFO [spike]   prop[183] Control.IsTemplateFocusTarget : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.524 8836/26084] INFO [spike]   prop[184] Control.IsTemplateKeyTipTarget : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.524 8836/26084] INFO [spike]   prop[185] IsTextScaleFactorEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.524 8836/26084] INFO [spike]   prop[186] Padding : Type=Windows.UI.Xaml.Thickness ValueType=Windows.UI.Xaml.Thickness ItemType=(null) bits=0x0 chainIdx=1 value=0,0,0,0
[01:01:11.525 8836/26084] INFO [spike]   prop[187] RequiresPointer : Type=Windows.UI.Xaml.Controls.RequiresPointer ValueType=Windows.UI.Xaml.Controls.RequiresPointer ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.525 8836/26084] INFO [spike]   prop[188] TabIndex : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=2147483647
[01:01:11.525 8836/26084] INFO [spike]   prop[189] TabNavigation : Type=Windows.UI.Xaml.Input.KeyboardNavigationMode ValueType=Windows.UI.Xaml.Input.KeyboardNavigationMode ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.525 8836/26084] INFO [spike]   prop[190] Template : Type=Windows.UI.Xaml.Controls.ControlTemplate ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.526 8836/26084] INFO [spike]   prop[191] UseSystemFocusVisuals : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.526 8836/26084] INFO [spike]   prop[192] VerticalContentAlignment : Type=Windows.UI.Xaml.VerticalAlignment ValueType=Windows.UI.Xaml.VerticalAlignment ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.526 8836/26084] INFO [spike]   prop[193] VerticalContentAlignment : Type=Windows.UI.Xaml.VerticalAlignment ValueType=Windows.UI.Xaml.VerticalAlignment ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.527 8836/26084] INFO [spike]   prop[194] XYFocusDown : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.527 8836/26084] INFO [spike]   prop[195] XYFocusLeft : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.527 8836/26084] INFO [spike]   prop[196] XYFocusRight : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.528 8836/26084] INFO [spike]   prop[197] XYFocusUp : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.528 8836/26084] INFO [spike]   prop[198] Canvas.Left : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.528 8836/26084] INFO [spike]   prop[199] Canvas.Top : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.529 8836/26084] INFO [spike]   prop[200] Canvas.ZIndex : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.529 8836/26084] INFO [spike]   prop[201] CommandingContainer.CommandingContainer : Type=Windows.UI.Xaml.Controls.CommandingContainer ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.529 8836/26084] INFO [spike]   prop[202] Content : Type=Windows.Foundation.Object ValueType=Windows.UI.Xaml.Controls.Border ItemType=(null) bits=0x1 chainIdx=0 value=112226120
[01:01:11.529 8836/26084] INFO [spike]   prop[203] Content : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.530 8836/26084] INFO [spike]   prop[204] ContentTemplate : Type=Windows.UI.Xaml.DataTemplate ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.530 8836/26084] INFO [spike]   prop[205] ContentTemplateSelector : Type=Windows.UI.Xaml.Controls.DataTemplateSelector ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.530 8836/26084] INFO [spike]   prop[206] ContentTransitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474872280
[01:01:11.531 8836/26084] INFO [spike]   prop[207] Grid.Column : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.531 8836/26084] INFO [spike]   prop[208] Grid.ColumnSpan : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.531 8836/26084] INFO [spike]   prop[209] Grid.Row : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.532 8836/26084] INFO [spike]   prop[210] Grid.RowSpan : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.532 8836/26084] INFO [spike]   prop[211] RelativePanel.Above : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.532 8836/26084] INFO [spike]   prop[212] RelativePanel.AlignBottomWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.533 8836/26084] INFO [spike]   prop[213] RelativePanel.AlignBottomWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.533 8836/26084] INFO [spike]   prop[214] RelativePanel.AlignHorizontalCenterWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.533 8836/26084] INFO [spike]   prop[215] RelativePanel.AlignHorizontalCenterWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.533 8836/26084] INFO [spike]   prop[216] RelativePanel.AlignLeftWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.534 8836/26084] INFO [spike]   prop[217] RelativePanel.AlignLeftWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.534 8836/26084] INFO [spike]   prop[218] RelativePanel.AlignRightWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.535 8836/26084] INFO [spike]   prop[219] RelativePanel.AlignRightWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.535 8836/26084] INFO [spike]   prop[220] RelativePanel.AlignTopWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.535 8836/26084] INFO [spike]   prop[221] RelativePanel.AlignTopWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.536 8836/26084] INFO [spike]   prop[222] RelativePanel.AlignVerticalCenterWith : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.536 8836/26084] INFO [spike]   prop[223] RelativePanel.AlignVerticalCenterWithPanel : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.537 8836/26084] INFO [spike]   prop[224] RelativePanel.Below : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.537 8836/26084] INFO [spike]   prop[225] RelativePanel.LeftOf : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.538 8836/26084] INFO [spike]   prop[226] RelativePanel.RightOf : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.538 8836/26084] INFO [spike]   prop[227] VariableSizedWrapGrid.ColumnSpan : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.538 8836/26084] INFO [spike]   prop[228] VariableSizedWrapGrid.RowSpan : Type=Windows.Foundation.Int32 ValueType=Windows.Foundation.Int32 ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.539 8836/26084] INFO [spike]   prop[229] ScrollViewer.BringIntoViewOnFocusChange : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.539 8836/26084] INFO [spike]   prop[230] ScrollViewer.CanContentRenderOutsideBounds : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.540 8836/26084] INFO [spike]   prop[231] ComputedHorizontalScrollBarVisibility : Type=Windows.UI.Xaml.Visibility ValueType=Windows.UI.Xaml.Visibility ItemType=(null) bits=0x2 chainIdx=0 value=1
[01:01:11.540 8836/26084] INFO [spike]   prop[232] ComputedHorizontalScrollBarVisibility : Type=Windows.UI.Xaml.Visibility ValueType=Windows.UI.Xaml.Visibility ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.540 8836/26084] INFO [spike]   prop[233] ComputedVerticalScrollBarVisibility : Type=Windows.UI.Xaml.Visibility ValueType=Windows.UI.Xaml.Visibility ItemType=(null) bits=0x2 chainIdx=0 value=1
[01:01:11.541 8836/26084] INFO [spike]   prop[234] ComputedVerticalScrollBarVisibility : Type=Windows.UI.Xaml.Visibility ValueType=Windows.UI.Xaml.Visibility ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.541 8836/26084] INFO [spike]   prop[235] ExtentHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=48
[01:01:11.542 8836/26084] INFO [spike]   prop[236] ExtentHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.542 8836/26084] INFO [spike]   prop[237] ExtentWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=1920
[01:01:11.543 8836/26084] INFO [spike]   prop[238] ExtentWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.543 8836/26084] INFO [spike]   prop[239] HorizontalAnchorRatio : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.543 8836/26084] INFO [spike]   prop[240] HorizontalOffset : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.544 8836/26084] INFO [spike]   prop[241] ScrollViewer.HorizontalScrollBarVisibility : Type=Windows.UI.Xaml.Controls.ScrollBarVisibility ValueType=Windows.UI.Xaml.Controls.ScrollBarVisibility ItemType=(null) bits=0x0 chainIdx=0 value=2
[01:01:11.544 8836/26084] INFO [spike]   prop[242] ScrollViewer.HorizontalScrollBarVisibility : Type=Windows.UI.Xaml.Controls.ScrollBarVisibility ValueType=Windows.UI.Xaml.Controls.ScrollBarVisibility ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.545 8836/26084] INFO [spike]   prop[243] ScrollViewer.HorizontalScrollMode : Type=Windows.UI.Xaml.Controls.ScrollMode ValueType=Windows.UI.Xaml.Controls.ScrollMode ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.545 8836/26084] INFO [spike]   prop[244] ScrollViewer.HorizontalScrollMode : Type=Windows.UI.Xaml.Controls.ScrollMode ValueType=Windows.UI.Xaml.Controls.ScrollMode ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.545 8836/26084] INFO [spike]   prop[245] HorizontalSnapPointsAlignment : Type=Windows.UI.Xaml.Controls.Primitives.SnapPointsAlignment ValueType=Windows.UI.Xaml.Controls.Primitives.SnapPointsAlignment ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.546 8836/26084] INFO [spike]   prop[246] HorizontalSnapPointsType : Type=Windows.UI.Xaml.Controls.SnapPointsType ValueType=Windows.UI.Xaml.Controls.SnapPointsType ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.546 8836/26084] INFO [spike]   prop[247] ScrollViewer.IsDeferredScrollingEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.547 8836/26084] INFO [spike]   prop[248] ScrollViewer.IsHorizontalRailEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.547 8836/26084] INFO [spike]   prop[249] ScrollViewer.IsHorizontalScrollChainingEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.548 8836/26084] INFO [spike]   prop[250] ScrollViewer.IsScrollInertiaEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.548 8836/26084] INFO [spike]   prop[251] ScrollViewer.IsVerticalRailEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.548 8836/26084] INFO [spike]   prop[252] ScrollViewer.IsVerticalScrollChainingEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.549 8836/26084] INFO [spike]   prop[253] ScrollViewer.IsZoomChainingEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.549 8836/26084] INFO [spike]   prop[254] ScrollViewer.IsZoomInertiaEnabled : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.550 8836/26084] INFO [spike]   prop[255] LeftHeader : Type=Windows.UI.Xaml.UIElement ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.550 8836/26084] INFO [spike]   prop[256] MaxZoomFactor : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=10
[01:01:11.550 8836/26084] INFO [spike]   prop[257] MinZoomFactor : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0.1
[01:01:11.551 8836/26084] INFO [spike]   prop[258] ReduceViewportForCoreInputViewOcclusions : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.551 8836/26084] INFO [spike]   prop[259] ScrollableHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=0
[01:01:11.552 8836/26084] INFO [spike]   prop[260] ScrollableHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.552 8836/26084] INFO [spike]   prop[261] ScrollableWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=0
[01:01:11.552 8836/26084] INFO [spike]   prop[262] ScrollableWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.553 8836/26084] INFO [spike]   prop[263] TopHeader : Type=Windows.UI.Xaml.UIElement ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.553 8836/26084] INFO [spike]   prop[264] TopLeftHeader : Type=Windows.UI.Xaml.UIElement ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.554 8836/26084] INFO [spike]   prop[265] VerticalAnchorRatio : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.554 8836/26084] INFO [spike]   prop[266] VerticalOffset : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.554 8836/26084] INFO [spike]   prop[267] ScrollViewer.VerticalScrollBarVisibility : Type=Windows.UI.Xaml.Controls.ScrollBarVisibility ValueType=Windows.UI.Xaml.Controls.ScrollBarVisibility ItemType=(null) bits=0x0 chainIdx=0 value=2
[01:01:11.555 8836/26084] INFO [spike]   prop[268] ScrollViewer.VerticalScrollBarVisibility : Type=Windows.UI.Xaml.Controls.ScrollBarVisibility ValueType=Windows.UI.Xaml.Controls.ScrollBarVisibility ItemType=(null) bits=0x0 chainIdx=1 value=3
[01:01:11.555 8836/26084] INFO [spike]   prop[269] ScrollViewer.VerticalScrollMode : Type=Windows.UI.Xaml.Controls.ScrollMode ValueType=Windows.UI.Xaml.Controls.ScrollMode ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.556 8836/26084] INFO [spike]   prop[270] ScrollViewer.VerticalScrollMode : Type=Windows.UI.Xaml.Controls.ScrollMode ValueType=Windows.UI.Xaml.Controls.ScrollMode ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.556 8836/26084] INFO [spike]   prop[271] VerticalSnapPointsAlignment : Type=Windows.UI.Xaml.Controls.Primitives.SnapPointsAlignment ValueType=Windows.UI.Xaml.Controls.Primitives.SnapPointsAlignment ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.557 8836/26084] INFO [spike]   prop[272] VerticalSnapPointsType : Type=Windows.UI.Xaml.Controls.SnapPointsType ValueType=Windows.UI.Xaml.Controls.SnapPointsType ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.557 8836/26084] INFO [spike]   prop[273] ViewportHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=48
[01:01:11.557 8836/26084] INFO [spike]   prop[274] ViewportHeight : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.558 8836/26084] INFO [spike]   prop[275] ViewportWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=0 value=1920
[01:01:11.558 8836/26084] INFO [spike]   prop[276] ViewportWidth : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.559 8836/26084] INFO [spike]   prop[277] ZoomFactor : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Double ItemType=(null) bits=0x2 chainIdx=1 value=1
[01:01:11.559 8836/26084] INFO [spike]   prop[278] ScrollViewer.ZoomMode : Type=Windows.UI.Xaml.Controls.ZoomMode ValueType=Windows.UI.Xaml.Controls.ZoomMode ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.560 8836/26084] INFO [spike]   prop[279] ScrollViewer.ZoomMode : Type=Windows.UI.Xaml.Controls.ZoomMode ValueType=Windows.UI.Xaml.Controls.ZoomMode ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.560 8836/26084] INFO [spike]   prop[280] ZoomSnapPoints : Type= ValueType=Windows.Foundation.Object ItemType=(null) bits=0x22 chainIdx=1 value=0
[01:01:11.560 8836/26084] INFO [spike]   prop[281] ZoomSnapPointsType : Type=Windows.UI.Xaml.Controls.SnapPointsType ValueType=Windows.UI.Xaml.Controls.SnapPointsType ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.561 8836/26084] INFO [spike]   prop[282] VirtualizingStackPanel.IsVirtualizing : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x2 chainIdx=1 value=0
[01:01:11.561 8836/26084] INFO [spike]   prop[283] VirtualizingStackPanel.VirtualizationMode : Type=Windows.UI.Xaml.Controls.VirtualizationMode ValueType=Windows.UI.Xaml.Controls.VirtualizationMode ItemType=(null) bits=0x0 chainIdx=1 value=1
[01:01:11.562 8836/26084] INFO [spike]   prop[284] CenterPoint : Type=Windows.Foundation.Numerics.Vector3 ValueType=Windows.Foundation.Numerics.Vector3 ItemType=(null) bits=0x0 chainIdx=0 value=0.000000,0.000000,0.000000
[01:01:11.562 8836/26084] INFO [spike]   prop[285] Rotation : Type=Windows.Foundation.Double ValueType=Windows.Foundation.Single ItemType=(null) bits=0x0 chainIdx=0 value=0
[01:01:11.562 8836/26084] INFO [spike]   prop[286] RotationAxis : Type=Windows.Foundation.Numerics.Vector3 ValueType=Windows.Foundation.Numerics.Vector3 ItemType=(null) bits=0x0 chainIdx=0 value=0.000000,0.000000,1.000000
[01:01:11.563 8836/26084] INFO [spike]   prop[287] Scale : Type=Windows.Foundation.Numerics.Vector3 ValueType=Windows.Foundation.Numerics.Vector3 ItemType=(null) bits=0x0 chainIdx=0 value=1.000000,1.000000,1.000000
[01:01:11.563 8836/26084] INFO [spike]   prop[288] TransformMatrix : Type=Windows.Foundation.Numerics.Matrix4x4 ValueType=Windows.Foundation.Numerics.Matrix4x4 ItemType=(null) bits=0x0 chainIdx=0 value={1.000000,0.000000,0.000000,0.000000}, {0.000000,1.000000,0.000000,0.000000}, {0.000000,0.000000,1.000000,0.000000}, {0.000000,0.000000,0.000000,1.000000}
[01:01:11.563 8836/26084] INFO [spike]   prop[289] Translation : Type=Windows.Foundation.Numerics.Vector3 ValueType=Windows.Foundation.Numerics.Vector3 ItemType=(null) bits=0x0 chainIdx=0 value=0.000000,0.000000,0.000000
[01:01:11.565 8836/26084] INFO [spike]   prop[290] RevealBrush.State : Type=Microsoft.UI.Xaml.Media.RevealBrushState ValueType=Microsoft.UI.Xaml.Media.RevealBrushState ItemType=(null) bits=0x0 chainIdx=1 value=
[01:01:11.565 8836/26084] INFO [spike]   prop[291] RevealBrush.IsContainer : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.566 8836/26084] INFO [spike]   prop[292] BackdropMaterial.ApplyToRootOrPageBackground : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.566 8836/26084] INFO [spike]   prop[293] BackdropMaterial.State : Type=Windows.Foundation.Boolean ValueType=Windows.Foundation.Boolean ItemType=(null) bits=0x0 chainIdx=1 value=0
[01:01:11.567 8836/26084] INFO [spike]   prop[294] RecyclePool.PoolInstance : Type=Microsoft.UI.Xaml.Controls.RecyclePool ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.567 8836/26084] INFO [spike]   prop[295] RecyclePool.ReuseKey : Type=Windows.Foundation.String ValueType=Windows.Foundation.String ItemType=(null) bits=0x0 chainIdx=1 value=
[01:01:11.567 8836/26084] INFO [spike]   prop[296] RecyclePool.OriginTemplate : Type=Windows.UI.Xaml.DataTemplate ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.568 8836/26084] INFO [spike]   prop[297] ItemsRepeater.VirtualizationInfo : Type=Windows.Foundation.Object ValueType=Windows.Foundation.Object ItemType=(null) bits=0x20 chainIdx=1 value=0
[01:01:11.568 8836/26084] INFO [spike]   prop[298] DesiredSize : Type=Windows.Foundation.Size ValueType=Windows.Foundation.Size ItemType=(null) bits=0x2 chainIdx=1 value=1920x48
```

Descida a partir dele, so com a API de pull:

```
[01:01:11.569 8836/26084] INFO [spike] DOWN d=0 Windows.UI.Xaml.Controls.ScrollViewer Name=[] handle=55888840
[01:01:11.569 8836/26084] INFO [spike] [Content] -> handle=112226120 collection=0
[01:01:11.573 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=112226120) hr=0x00000000 sources=2 values=230
[01:01:11.573 8836/26084] INFO [spike]   prop[3] AutomationProperties.Annotations : Type=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ValueType=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ItemType=(null) bits=0x5 chainIdx=1 value=474869640
[01:01:11.574 8836/26084] INFO [spike]   prop[99] KeyboardAccelerators : Type=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ValueType=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ItemType=(null) bits=0x5 chainIdx=1 value=474869880
[01:01:11.575 8836/26084] INFO [spike]   prop[104] Lights : Type=Windows.UI.Xaml.Media.XamlLightCollection ValueType=Windows.UI.Xaml.Media.XamlLightCollection ItemType=(null) bits=0x7 chainIdx=1 value=474874680
[01:01:11.575 8836/26084] INFO [spike]   prop[118] Transitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474874920
[01:01:11.576 8836/26084] INFO [spike]   prop[128] VisualStateManager.VisualStateGroups : Type=Windows.UI.Xaml.Internal.VisualStateGroupCollection ValueType=Windows.UI.Xaml.Internal.VisualStateGroupCollection ItemType=(null) bits=0x7 chainIdx=1 value=471518744
[01:01:11.576 8836/26084] INFO [spike]   prop[138] FocusVisualPrimaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=466975176
[01:01:11.576 8836/26084] INFO [spike]   prop[140] FocusVisualSecondaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=468613640
[01:01:11.577 8836/26084] INFO [spike]   prop[151] Parent : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.UI.Xaml.Internal.RootScrollViewer ItemType=(null) bits=0x3 chainIdx=0 value=55888840
[01:01:11.578 8836/26084] INFO [spike]   prop[156] Triggers : Type=Windows.UI.Xaml.TriggerCollection ValueType=Windows.UI.Xaml.TriggerCollection ItemType=(null) bits=0x5 chainIdx=1 value=474875160
[01:01:11.578 8836/26084] INFO [spike]   prop[160] Background : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=0 value=474870328
[01:01:11.578 8836/26084] INFO [spike]   prop[166] Child : Type=Windows.UI.Xaml.UIElement ValueType=Windows.UI.Xaml.Controls.Grid ItemType=(null) bits=0x1 chainIdx=1 value=424857800
[01:01:11.579 8836/26084] INFO [spike]   prop[167] ChildTransitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474872760
[01:01:11.580 8836/26084] INFO [spike]   DOWN d=1 Windows.UI.Xaml.Controls.Border Name=[] handle=112226120
[01:01:11.580 8836/26084] INFO [spike]   [Child] -> handle=424857800 collection=0
[01:01:11.582 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=424857800) hr=0x00000000 sources=2 values=236
[01:01:11.582 8836/26084] INFO [spike]   prop[3] AutomationProperties.Annotations : Type=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ValueType=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ItemType=(null) bits=0x5 chainIdx=1 value=474867240
[01:01:11.584 8836/26084] INFO [spike]   prop[99] KeyboardAccelerators : Type=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ValueType=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ItemType=(null) bits=0x5 chainIdx=1 value=474865320
[01:01:11.584 8836/26084] INFO [spike]   prop[104] Lights : Type=Windows.UI.Xaml.Media.XamlLightCollection ValueType=Windows.UI.Xaml.Media.XamlLightCollection ItemType=(null) bits=0x7 chainIdx=1 value=474865560
[01:01:11.584 8836/26084] INFO [spike]   prop[118] Transitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474868200
[01:01:11.585 8836/26084] INFO [spike]   prop[128] VisualStateManager.VisualStateGroups : Type=Windows.UI.Xaml.Internal.VisualStateGroupCollection ValueType=Windows.UI.Xaml.Internal.VisualStateGroupCollection ItemType=(null) bits=0x7 chainIdx=1 value=471515928
[01:01:11.585 8836/26084] INFO [spike]   prop[139] FocusVisualPrimaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=466975176
[01:01:11.586 8836/26084] INFO [spike]   prop[141] FocusVisualSecondaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=468613640
[01:01:11.586 8836/26084] INFO [spike]   prop[151] Parent : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.UI.Xaml.Controls.Border ItemType=(null) bits=0x3 chainIdx=1 value=112226120
[01:01:11.587 8836/26084] INFO [spike]   prop[156] Triggers : Type=Windows.UI.Xaml.TriggerCollection ValueType=Windows.UI.Xaml.TriggerCollection ItemType=(null) bits=0x5 chainIdx=1 value=474866280
[01:01:11.587 8836/26084] INFO [spike]   prop[165] Children : Type=Windows.UI.Xaml.Controls.UIElementCollection ValueType=Windows.UI.Xaml.Controls.UIElementCollection ItemType=(null) bits=0x5 chainIdx=1 value=462849048
[01:01:11.588 8836/26084] INFO [spike]   prop[166] ChildrenTransitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474875400
[01:01:11.588 8836/26084] INFO [spike]   prop[177] ColumnDefinitions : Type=Windows.UI.Xaml.Controls.ColumnDefinitionCollection ValueType=Windows.UI.Xaml.Controls.ColumnDefinitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474865800
[01:01:11.589 8836/26084] INFO [spike]   prop[183] RowDefinitions : Type=Windows.UI.Xaml.Controls.RowDefinitionCollection ValueType=Windows.UI.Xaml.Controls.RowDefinitionCollection ItemType=(null) bits=0x5 chainIdx=1 value=474870840
[01:01:11.590 8836/26084] INFO [spike]     DOWN d=2 Windows.UI.Xaml.Controls.Grid Name=[] handle=424857800
[01:01:11.590 8836/26084] INFO [spike]     [Children] -> handle=462849048 collection=1
[01:01:11.590 8836/26084] INFO [spike]   GetCollectionCount(462849048) hr=0x00000000 count=2
[01:01:11.591 8836/26084] INFO [spike]   GetCollectionElements(462849048, n=0) hr=0x00000000 returned=0 ptr=000000001C3E48C0
[01:01:11.591 8836/26084] INFO [spike]   GetCollectionElements(462849048, n=2 in) hr=0x00000000 returned=2 ptr=000000001C50C020
[01:01:11.592 8836/26084] INFO [spike]     elem[0] idx=0 ValueType=Taskbar.TaskbarFrame bits=0x1 value=115203464
[01:01:11.592 8836/26084] INFO [spike]     elem[1] idx=1 ValueType=SystemTray.SystemTrayFrame bits=0x1 value=461666696
```

Dois fatos:

1. **`pElementCount` de `GetCollectionElements` e in/out, apesar de declarado
   `[out]` no `xamlOM.h`.** Com `n = 0` a chamada retorna `S_OK` e zero
   elementos (com ponteiro nao-nulo!); passando `n = GetCollectionCount(...)`
   vem os dois filhos com `bits=0x1` (`IsValueHandle`) e o handle em decimal no
   `Value`. Quem seguir a assinatura do header ao pe da letra conclui, errado,
   que colecao nao enumera.
2. Propriedades de filho unico (`Border.Child`, `ContentControl.Content`)
   funcionam sem colecao nenhuma (`[Child] -> handle=... collection=0`).

A ressalva seria aparece um nivel abaixo, no `Taskbar.TaskbarFrame` (que o stream
reporta com `children=1`). Estas sao **todas** as propriedades dele com
`IsValueHandle`:

```
[01:01:11.605 8836/26084] INFO [spike] GetPropertyValuesChain(down handle=115203464) hr=0x00000000 sources=4 values=265
[01:01:11.606 8836/26084] INFO [spike]   prop[3] AutomationProperties.Annotations : Type=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ValueType=Windows.UI.Xaml.Automation.AutomationAnnotationCollection ItemType=(null) bits=0x5 chainIdx=3 value=474868440
[01:01:11.607 8836/26084] INFO [spike]   prop[100] KeyboardAccelerators : Type=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ValueType=Windows.UI.Xaml.Input.KeyboardAcceleratorCollection ItemType=(null) bits=0x5 chainIdx=3 value=474866760
[01:01:11.607 8836/26084] INFO [spike]   prop[105] Lights : Type=Windows.UI.Xaml.Media.XamlLightCollection ValueType=Windows.UI.Xaml.Media.XamlLightCollection ItemType=(null) bits=0x7 chainIdx=3 value=474875640
[01:01:11.607 8836/26084] INFO [spike]   prop[119] Transitions : Type=Windows.UI.Xaml.Media.Animation.TransitionCollection ValueType=Windows.UI.Xaml.Media.Animation.TransitionCollection ItemType=(null) bits=0x5 chainIdx=3 value=474875880
[01:01:11.609 8836/26084] INFO [spike]   prop[129] VisualStateManager.VisualStateGroups : Type=Windows.UI.Xaml.Internal.VisualStateGroupCollection ValueType=Windows.UI.Xaml.Internal.VisualStateGroupCollection ItemType=(null) bits=0x7 chainIdx=3 value=471516184
[01:01:11.609 8836/26084] INFO [spike]   prop[139] FocusVisualPrimaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=3 value=466975176
[01:01:11.610 8836/26084] INFO [spike]   prop[141] FocusVisualSecondaryBrush : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=3 value=468613640
[01:01:11.610 8836/26084] INFO [spike]   prop[153] Parent : Type=Windows.UI.Xaml.DependencyObject ValueType=Windows.UI.Xaml.Controls.Grid ItemType=(null) bits=0x3 chainIdx=3 value=424857800
[01:01:11.611 8836/26084] INFO [spike]   prop[157] Triggers : Type=Windows.UI.Xaml.TriggerCollection ValueType=Windows.UI.Xaml.TriggerCollection ItemType=(null) bits=0x5 chainIdx=3 value=474873960
[01:01:11.611 8836/26084] INFO [spike]   prop[161] Background : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=1 value=474867688
[01:01:11.611 8836/26084] INFO [spike]   prop[162] Background : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=2 value=474862408
[01:01:11.612 8836/26084] INFO [spike]   prop[180] Foreground : Type=Windows.UI.Xaml.Media.Brush ValueType=Windows.UI.Xaml.Media.SolidColorBrush ItemType=(null) bits=0x1 chainIdx=3 value=430817912
[01:01:11.612 8836/26084] INFO [spike]   prop[195] Template : Type=Windows.UI.Xaml.Controls.ControlTemplate ValueType=Windows.UI.Xaml.Controls.ControlTemplate ItemType=(null) bits=0x1 chainIdx=1 value=471521784
[01:01:11.613 8836/26084] INFO [spike]   prop[196] Template : Type=Windows.UI.Xaml.Controls.ControlTemplate ValueType=Windows.UI.Xaml.Controls.ControlTemplate ItemType=(null) bits=0x1 chainIdx=2 value=471522552
[01:01:11.614 8836/26084] INFO [spike]       DOWN d=3 Taskbar.TaskbarFrame Name=[] handle=115203464
```

Nao ha `Children`, nem `Content`, nem `Child`. Ha `Template`
(um `ControlTemplate`, a definicao, nao a instancia) e ha `Parent`. Ou seja: o
filho visual de um controle com template - que e o que a taskbar inteira e -
**nao aparece na cadeia de propriedades**. O `SystemTray.SystemTrayFrame` na
sequencia se comporta igual (a descida para no `DOWN d=3`).

**Conclusao Q2: o pull enumera filhos de `Panel`/`Border`/`ContentControl`, mas
nao atravessa template. Uma arvore montada so por pull para na primeira
`Taskbar.*Frame`.**

---

## Q3 - da para chegar na raiz da taskbar a partir do que Q1 da?

Do `GetUiLayer`, nao: `UP level=0 ... parent=0` (linha ja citada em Q1) e
`Children` com contagem 0. Ele nao esta ligado a arvore da taskbar.

A arvore real, como o stream de mutacoes a reporta (primeiros 12 nos):

```
[01:01:11.450 8836/26084] INFO [spike] Q2 reported[0] Windows.UI.Xaml.Hosting.DesktopWindowXamlSource handle=431130496 parent=0 children=2
[01:01:11.450 8836/26084] INFO [spike] Q2 reported[1] Windows.UI.Xaml.PopupRoot handle=461657192 parent=431130496 children=0
[01:01:11.450 8836/26084] INFO [spike] Q2 reported[2] Windows.UI.Xaml.Internal.RootScrollViewer handle=55888840 parent=431130496 children=1
[01:01:11.451 8836/26084] INFO [spike] Q2 reported[3] Windows.UI.Xaml.Controls.ScrollContentPresenter handle=459932632 parent=55888840 children=1
[01:01:11.451 8836/26084] INFO [spike] Q2 reported[4] Windows.UI.Xaml.Controls.Border handle=112226120 parent=459932632 children=1
[01:01:11.451 8836/26084] INFO [spike] Q2 reported[5] Windows.UI.Xaml.Controls.Grid handle=424857800 parent=112226120 children=2
[01:01:11.452 8836/26084] INFO [spike] Q2 reported[6] Taskbar.TaskbarFrame handle=115203464 parent=424857800 children=1
[01:01:11.452 8836/26084] INFO [spike] Q2 reported[7] SystemTray.SystemTrayFrame handle=461666696 parent=424857800 children=1
[01:01:11.452 8836/26084] INFO [spike] Q2 reported[8] Windows.UI.Xaml.Controls.Grid handle=464484088 parent=115203464 children=4
[01:01:11.452 8836/26084] INFO [spike] Q2 reported[9] Windows.UI.Xaml.Controls.Grid handle=468477432 parent=461666696 children=8
[01:01:11.452 8836/26084] INFO [spike] Q2 reported[10] Taskbar.TaskbarBackground handle=466934264 parent=464484088 children=1
[01:01:11.453 8836/26084] INFO [spike] Q2 reported[11] Microsoft.UI.Xaml.Controls.ItemsRepeater handle=464008952 parent=464484088 children=15
```

Esta e a evidencia pedida de nomes de tipo reais, alguns niveis:
`DesktopWindowXamlSource` -> `PopupRoot` + `RootScrollViewer` ->
`ScrollContentPresenter` -> `Border` -> `Grid` -> `Taskbar.TaskbarFrame` +
`SystemTray.SystemTrayFrame` -> `Grid` -> `Taskbar.TaskbarBackground` +
`Microsoft.UI.Xaml.Controls.ItemsRepeater` (15 filhos - os botoes).

`Parent` existe e e utilizavel em elementos reais (no `TaskbarFrame` acima:
`Parent : ... ValueType=Windows.UI.Xaml.Controls.Grid bits=0x3 value=424857800`,
que e exatamente o pai que o stream reportou). Ou seja, subir funciona - mas so
depois de ter um elemento real na mao, o que Q1 diz que exige assinatura. Note
tambem que `DesktopWindowXamlSource` (a raiz do stream) **nao** e conhecido pelo
pull (`GetPropertyValuesChain ... hr=0x80070490`): a raiz alcancavel por
propriedade e o `RootScrollViewer`.

---

## O que isso significa para o desenho (A) vs (B)

**(A) pull puro nao e viavel.** Duas paredes independentes, cada uma sozinha ja
fatal:

1. Nao existe ponto de entrada. `GetUiLayer` da um `Grid` solto e vazio,
   `GetApplication` da um objeto que o tree service nao conhece, `HitTest` e
   `E_INVALIDARG` e `Window.Current.Content` e nulo. Sem assinar, nao ha
   primeiro handle.
2. Mesmo com um handle na mao, a descida por propriedade para no primeiro
   controle com template - e `Taskbar.TaskbarFrame` esta no terceiro nivel.

**(B) assinar e o unico caminho** para ter a arvore da taskbar, e o custo real
dele ficou menor do que parecia: a enxurrada inicial chega **sincrona dentro da
chamada de `AdviseVisualTreeChange`** (o log mostra `reported=200` ja no retorno
dela, no mesmo milissegundo, na mesma thread do `SetSite`). Nao houve thread de
callback nova nesta medicao. O `ParentChildRelation` de cada report ja traz
`Parent`, `Child` e `ChildIndex`, e o `VisualElement` traz `Type`, `Name` e
`NumChildren` - da para montar a arvore inteira so com o stream, sem uma unica
chamada de pull.

Detalhe de custo que o spike mediu de graca: a fila de release adiada continua
necessaria para o regime **permanente** (mutacoes que chegam depois, de dentro
do Leave walk do explorer), mas nao para o snapshot inicial: liberar os 200
handles depois do `Unadvise`, fora de qualquer callback, funcionou sem
incidente (`Q2 released 200 reported handles`).

Forma sugerida para a Task 5, se ajudar: `advise -> capturar a enxurrada ->
unadvise -> imprimir`, que da a arvore em texto sem manter assinatura viva nem
precisar do dreno adiado. A assinatura permanente (e o dreno) so passa a ser
necessaria quando o Plano 3 quiser reagir a mudancas.

A API de pull ainda tem uso pontual e barato: `GetPropertyValuesChain` num
handle que o stream ja reportou funciona bem, e e assim que se le
`Background`/`Foreground`/etc de um elemento especifico na hora de estilizar.
Se um dia for usada para colecoes, lembrar do `pElementCount` in/out.

---

## Notas de execucao

- O limite de 200 elementos e do proprio spike (`seen.size() < 200`), nao do
  XAML: a enxurrada inicial e maior que isso.
- `IXamlDiagnosticsTestHooks` estava disponivel; nenhum handle ficou pendurado
  (`done, handles released=208` = 200 do stream + 8 da travessia).
- O aviso `C4002 GetCurrentTime` vem de `Windows.UI.Xaml.Media.Animation.h`,
  incluido so pela sonda; some junto com o spike.