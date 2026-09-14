# Estado do trabalho

Atualizado em 2026-09-14. Este é o documento de passagem de bastão: onde o
trabalho parou, o que já está decidido e o que falta fazer, em ordem.

Leia antes: `AGENTS.md` na raiz (restrições invioláveis, como compilar e testar,
como o trabalho é organizado).

## Planos

| Plano | Escopo | Estado |
|---|---|---|
| 1 | `styler_core`: seletores, regras, temas; 55 temas convertidos byte a byte | mesclado em `main` |
| 2 | TAP carrega no explorer; exporta a árvore visual | mesclado em `main` |
| 3 | Aplicar e desfazer estilos; assinatura permanente; CLI `apply/reset` | mesclado em `main` |
| 3b | Fidelidade: blur real, variáveis de estilo, reciclagem, timeout no fan-out | **em execução**, branch `plano-3b-fidelidade` |
| 4 | App de bandeja em C# .NET 10, sempre ligado, sobrevive a restart do explorer | não iniciado |

`main` = `be0d3e6` (em `origin`). O branch `plano-3b-fidelidade` está em `origin`
até `6c68ec6`; os commits posteriores são **locais** e precisam de push.

## Plano 3b — onde parou

Plano: `docs/superpowers/plans/2026-09-14-plano-3b-fidelidade.md` (7 tasks).
Decisões e histórico de execução: `docs/superpowers/plano-3b-decisoes.md`.

| Task | O quê | Estado | Commits |
|---|---|---|---|
| 1 | `SendMessageTimeoutW` no fan-out; `NumChildren` cruzado com filhos entregues | concluída, revisada | `573b8ca`, `b980ba5` |
| 2 | `BlurSpec` + `ParseWindhawkBlur` no core; `AcrylicBrush` vira fallback | concluída, revisada | `978fd63` |
| 3 | Cinco efeitos D2D sobre `IGraphicsEffectD2D1Interop` do SDK + ruído | concluída, revisada | `943a5c8`, `6c68ec6` |
| 4 | `XamlBlurBrush` por elemento; blur real na taskbar | concluída, revisada | `857a493`, `b080abc` |
| 5 | Avaliador de `{{...}}` no core (puro) + 5 limpezas parqueadas | concluída, revisada | `2353759`, `fe3b95b` |
| 6 | Capturas `Prop=>Var` e valores dinâmicos no TAP | concluída, revisada | `844a4b7`, `127e8fe` |
| 7 | Reciclagem do `ItemsRepeater` | **não iniciada — começa por um spike** | — |

Suítes no último estado verde: **core 129/129** (7808 asserções) · **tap 24/24**
(52 asserções) · 0 warnings novos sob `/W4`. Os únicos warnings aceitos são os
pré-existentes `C5285` (doctest vendorizado) e `C4002` (headers do SDK).

Todas as tasks acima passaram por revisão independente (conformidade com a
especificação mais qualidade de código) e, quando havia achados, por uma rodada
de correção e uma re-revisão escopada. Nada ficou com achado em aberto.

O que o Plano 3b já entregou, verificado ao vivo: blur de composição real
(borrão gaussiano, tinta, saturação, luminosidade e ruído) em vez da aproximação
`AcrylicBrush`; troca de tema claro e escuro reconstruindo o brush; capturas
`Prop=>Var` e valores `{{...}}` aplicados por elemento, com as pílulas do tema
`Pills` acompanhando a largura do rótulo de cada botão, em tempo real.

## O que falta fazer, em ordem

### 1. Dois ajustes pequenos, decididos e ainda não feitos

Vieram de um achado do smoke da Task 6 (decisão 11 do registro):

- **Nome da propriedade na linha de erro.** `ApplyBucketForState`
  (`src/tap/style_engine.cpp`) loga apenas o id do elemento e o estado visual
  quando uma aplicação falha. Quem diagnosticou a rajada de erros teve de
  deduzir a propriedade pelo conteúdo do tema. Incluir o nome da propriedade
  melhora todo diagnóstico futuro.
- **Documentar a rajada transitória** em `docs/smoke-test.md`. Durante um
  rebuild do painel de botões (por exemplo ao mudar o agrupamento da taskbar),
  `{{BtnW-6}}` resolve para `-6` enquanto a largura passa por zero, e o XAML
  rejeita largura mínima negativa. Foram 136 erros numa rajada de 777 ms, que
  se curam sozinhos e não têm efeito visual. Sem essa nota, o critério
  "zero erros" do roteiro de smoke deixa de significar alguma coisa.

**Decisão já tomada:** não adicionar guarda de valor negativo agora. Exigiria
uma lista de quais propriedades não aceitam negativo, o que é aumento de escopo
e divergência do upstream, que calcula e escreve o mesmo valor. A decisão sobre
a guarda foi levada para a revisão final do branch.

### 2. Task 7 — reciclagem do `ItemsRepeater`, começando por um spike

**Não escreva código antes do spike.** A task inteira pode sair de escopo, e o
plano diz explicitamente que essa é uma saída legítima. O texto completo está na
seção "Task 7" do plano; o essencial:

A pergunta é se a taskbar realmente recicla elementos de item sem que a
diagnostics reporte remoção e adição. Se reciclar, os estilos casados para o
item antigo permanecem no elemento quando ele volta representando outro
aplicativo.

**Roteiro:** com o tema `Pills` aplicado (tem estilos por botão, é onde o
defeito apareceria), abra seis aplicativos, feche os três do meio, abra outros
três. Em nível `Debug`, o log registra uma linha por botão novo estilizado. Se
cada botão novo produzir sua linha, não há reciclagem observável e a task sai de
escopo. Se um botão aparecer sem linha correspondente e visualmente errado (a
pílula de outro aplicativo, largura errada, rótulo com o espaçamento do
anterior), o defeito existe.

**Segunda pergunta, se o defeito existir:** qual sinal delata a reciclagem. O
plano traz o código de instrumentação temporária para medir `DataContextChanged`
contra a troca de `Visibility`, e as quatro saídas possíveis com o que fazer em
cada uma.

**Restrição que não muda:** sem projeção `Microsoft.UI.Xaml`. O
`muxc::ItemsRepeater` que o upstream usa está fora. Se nenhum sinal de
`Windows.UI.Xaml` servir, a task é cortada com o motivo registrado, e o Plano 4
decide se a restrição muda.

**Nota de implementação já decidida:** se a task for adiante, o lookup de
elemento para identificador precisa obter o handle como o upstream faz em
`HandleFromInspectable` (`vendor:10961`), consultando a interface `IInspectable`,
e não pelo ponteiro do `FrameworkElement`. Ver decisão 3 do registro.

### 3. Revisão final do branch inteiro

Depois da Task 7, uma revisão do branch completo (`be0d3e6..HEAD`) no modelo
mais capaz disponível, seguida de **uma** onda de correção e **uma** re-revisão
escopada. Itens parqueados que essa revisão deve julgar:

- **Da Task 3:** `DetachSource` poderia usar `copy_to_abi` em vez do cast de
  identidade; `StoreAsync` com status diferente de `Started` é ignorado
  silenciosamente; `winrt_common.h` faz todos os arquivos do TAP compilarem os
  headers de composition, a 15 a 20 segundos cada, para benefício de poucos.
- **Da Task 6:** `ResolveProperty` não tem cache, gerando duas análises de XAML
  por estilo dinâmico por elemento na enxurrada inicial (51 valores dinâmicos em
  185 elementos); a escolha do capturador mais próximo é aritmética pura sobre
  cadeias e sequências, a peça mais sujeita a erro sutil, mas hoje só é
  verificável a olho porque exige um elemento vivo, então vale extraí-la para
  ser testável sem XAML; `ApplyBucketForState` reaplica o bucket inteiro a cada
  propagação, não só a propriedade que mudou.
- **A guarda de valor negativo** da decisão 11.

### 4. Integração

Com a revisão final limpa: rodar a suíte completa, mesclar
`plano-3b-fidelidade` em `main` localmente (fast-forward, como nos planos
anteriores), apagar o branch, e preservar o registro de decisões em
`docs/superpowers/`. Nunca commitar direto em `main`. Nunca fazer push sem o
dono do repositório pedir.

### 5. Plano 4

O app de bandeja em C# .NET 10: sempre ligado, sobrevive a reinício do explorer,
troca de tema pela bandeja. Ainda não tem plano escrito. Herda dois itens da
revisão final do Plano 3: o contador de handles VIVOS (especificação §7.2) e o
risco de deadlock em `SetSite(nullptr)` (`UnregisterWaitEx(INVALID_HANDLE_VALUE)`
mais o ramo `!site` ignorando `Deferred`).

## Questão em aberto: o reinício do explorer

Em 2026-09-14 às 14:13:13 o shell reiniciou, cerca de três minutos depois de um
smoke terminar em estado de reset, cem milissegundos após uma linha
`new XAML host` no log do TAP. Nenhum agente deu o comando.

**Evidência levantada, que aponta para não ser falha nossa:**

- Zero eventos "Application Error 1000" naquele dia. Uma falha dentro da nossa
  DLL geraria um, nomeando o módulo culpado.
- O evento é um "Winlogon 1002 — o shell parou" sozinho, que é a assinatura de
  um encerramento forçado, não de uma falha.
- Houve quatro reinícios do shell naquele dia (09:20, 09:41, 13:48, 14:13),
  batendo com os reinícios do explorer feitos ao longo dos smokes para destravar
  a DLL na hora de relinkar. O das 14:13 é o último deles.
- A revisão da Task 4 não encontrou nenhum ponto de falha no diff e mostrou que,
  em estado de reset, nenhum código da Task 4 roda na inicialização de um host.
- O smoke longo da Task 6, posterior, manteve o mesmo PID do começo ao fim, com
  185 elementos estilizados e aplicações e resets sucessivos.

**O que ainda não foi feito:** o repro adversarial que a revisão pediu antes do
merge. Armar captura de dump para o explorer
(`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\explorer.exe`,
`DumpType=2`, precisa de elevação), depois alternar `apply Command_Center` e
`reset` em laço enquanto se força a criação de hosts XAML novos, vigiando uma
falha dentro de cerca de 100 ms de uma linha `new XAML host`. Um laço limpo
rebaixa a questão; um dump apontando para a nossa DLL a promove a crítica. Isso
reinicia o explorer várias vezes, então ficou para o dono da máquina decidir
quando.

## Pendências de teste manual

`docs/smoke-test.md` itens 7 a 9, que exigem a máquina do usuário: segundo
monitor; menu Iniciar e central de notificações com um tema aplicado; dez
minutos parado conferindo que a contagem de handles retidos bate com o número de
elementos estilizados e não cresce.

## Onde estão as evidências que não estão no git

O diretório `.superpowers/sdd/2026-09-14-plano-3b-fidelidade/` tem um
`.gitignore` com `*`, então **nada dele está versionado**. Ele contém, na máquina
onde o trabalho foi feito: o registro de execução bruto, o relatório de cada task
com as evidências de teste, os diffs de cada revisão, e 22 capturas de tela dos
smokes (blur real com janela por baixo, claro e escuro, grão, pílulas seguindo o
rótulo, antes e depois do reset). O que importa dessas evidências foi destilado
em `docs/superpowers/plano-3b-decisoes.md`, que é versionado. Se precisar dos
originais e eles não existirem mais, não há como recuperá-los do git.

## Branches obsoletos

`plano-1-nucleo-e-temas`, `spike-pull-walk`, `spike-standing-crash` podem ser
apagados.
