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
| 3b | Fidelidade: blur real, variáveis de estilo, reciclagem, timeout no fan-out | código revisado e smoke concluído; **gate de pré-merge aberto** |
| 4 | App de bandeja em C# .NET 10, sempre ligado, sobrevive a restart do explorer | não iniciado |

`main` = `be0d3e6` (em `origin`). `origin/plano-3b-fidelidade` está em
`0db9622`: o handoff anterior já foi enviado. A retomada está em commits
locais de `plano-3b-fidelidade`: correções em `27ff1a8` e documentação de
fechamento em seguida. Nenhum merge nem push foi feito nesta retomada.

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
| 7 | Reciclagem do `ItemsRepeater` | spike concluído; sem defeito observável no cenário; sem código adicional | `plano-3b-spike-reciclagem.md` |

Validação da retomada: build completo com 0 warnings novos sob `/W4`;
`ctest --verbose` com **core 129/129** (7808 asserções) e **tap 30/30**
(92 asserções), saída 0. Os 55 temas continuam iguais byte a byte após
extração; verificações de APIs proibidas e projeção WinUI sem ocorrências;
includes WinRT continuam concentrados em `winrt_common.h`. `pytest tools/`
teve 22 testes verdes nesta retomada. Smoke final das correções concluído;
evidências e limites na seção abaixo.

As implementações das Tasks 1 a 6 passaram por revisão independente e correções.
A revisão final encontrou os problemas adicionais descritos abaixo, já
corrigidos e re-revisados. O gate operacional do reinício de Explorer continua
aberto; revisão estática e suíte verde não o encerram.

O que o Plano 3b já entregou, verificado ao vivo: blur de composição real
(borrão gaussiano, tinta, saturação, luminosidade e ruído) em vez da aproximação
`AcrylicBrush`; troca de tema claro e escuro reconstruindo o brush; capturas
`Prop=>Var` e valores `{{...}}` aplicados por elemento, com as pílulas do tema
`Pills` acompanhando a largura do rótulo de cada botão, em tempo real.

## Retomada no Codex — o que foi fechado

### Ajustes da decisão 11

`ApplyBucketForState` agora inclui o nome da propriedade no erro.
`docs/smoke-test.md` documenta a rajada transitória de `MinWidth` negativo com
`Pills`: no smoke da Task 6, `{{BtnW-6}}` passou a `-6` durante rebuild e gerou
136 erros em 777 ms. A exceção fica restrita a essa causa durante a transição,
com recuperação visual e ausência de erros em regime. O smoke da retomada
também confirmou `MaxWidth` na mesma expressão; a exceção foi ampliada somente
para essas duas propriedades. Não foi introduzida guarda de negativos.

### Task 7 — spike concluído

Com `Pills`, seis aplicativos Win32 de identidades distintas foram abertos;
os três do meio foram fechados e substituídos por outros três. O snapshot já
continha um conjunto de botões pré-criados: duas árvores com 12
`TaskListButton` cada, e 24 linhas iniciais de estilização. As trocas de apps
não produziram novas linhas para esses botões raiz. Reaplicar `Pills` produziu
24 linhas novamente, sem diferença visual nas capturas antes/depois.

**Conclusão: sem defeito observável neste cenário.** Isso não prova ausência
de reciclagem. A Task 7 foi encerrada sem código de detecção/reaplicação
adicional, como permitido pelo plano. O ensaio usou ícones agrupados, sem
rótulos, e a imagem de um monitor; outros temas, configurações e versões de
Windows continuam fora dessa evidência.

Explorer PID **22488** estável no ensaio; **0 `ERR` desse PID**; reset às
19:47:26 restaurou 223 elementos e o dreno registrou `0 held`. Este último
número é por lote e não comprova a contagem global de handles vivos.
Roteiro, horários e arquivos: [registro do spike](superpowers/plano-3b-spike-reciclagem.md).

### Correções da revisão final

- Dependências dinâmicas agora são a união dos estados efetivos de cada
  propriedade. Regras derrotadas não alteram inscrições; uma linha estática
  posterior remove o template dinâmico e suas dependências daquele estado.
- Falha de parsing XAML mantém template e dependências para tentar novamente
  quando a variável mudar. Na reexpansão, o valor inválido agora é retirado:
  aplica-se o estado padrão ou `Unapply`, em vez de manter o último valor bom.
- A reexpansão mantém uma referência local ao tema e descarta resultados se
  elemento/tema forem substituídos durante reentrância. A montagem inicial
  também valida a geração antes de reutilizar estado após resolução de XAML.
- `DetachSource` devolve a interface `IGraphicsEffectSource` correta via
  `copy_to_abi`, preservando seu endereço em vez de converter o `IUnknown`
  canônico. `StoreAsync` propaga erros/cancelamento; se ainda estiver em curso,
  cancela e segue pelo fallback existente, sem bloquear a thread de UI.
- Quatro testes de estados/dependências e dois de efeitos/stream de ruído
  elevaram a suíte TAP de 24 para 30 casos. Os testes puros não exercitam
  reentrância do Explorer, precedência entre regras no motor nem rejeição
  real de um valor pelo XAML; esses limites permanecem explícitos.

Durante a primeira execução dos novos testes houve uma AV no encerramento do
processo de testes: o apartment COM era desinicializado antes do cache de
stream `thread_local`. A fixture foi corrigida para manter o apartment até
depois do cache; a execução completa passou com saída 0. Esse defeito da
fixture não identifica a causa do reinício histórico do Explorer.

### Smoke final das correções — concluído

DLL SHA-256: `E7B90B57D2B566E1ACD7EC0F5CADED8A40B4378C56F17F74AA1998E1B442724A`.
Explorer PID **19132**, iniciado pelo restart autorizado das 19:47:50 para
liberar a DLL e carregar a nova compilação.

| Horário | Tema | Medição inicial |
|---|---|---|
| 19:49:00 | Pills | 157 elementos, 664 propriedades, 36 brushes, 6 variáveis |
| 19:50:00 | Command_Center | 66 elementos, 268 propriedades, 52 brushes, 0 fallbacks |
| 19:50:03 | TranslucentTaskbar | aplicação realizada |
| 19:50:06 | Pills | 155 elementos, 638 propriedades, 34 brushes, 6 variáveis |

Ao abrir três janelas, foram registrados **28 `ERR`** entre 19:52:12.226 e
19:52:12.730 (504 ms), todos `0x80070057` em `MinWidth`/`MaxWidth` com
`{{BtnW-6}}`. A captura `final-pills-new-buttons.png` mostrou recuperação
visual; fechar as janelas não gerou nova rajada. O monitor inicial interrompeu
a contagem por encontrar erros; a observação foi retomada para verificar
ausência de novas linhas após essas 28 conhecidas. **Não houve zero erros
totais nesse smoke.** A observação de dez minutos, de 19:50:44 a 20:00:44,
terminou com PID 19132 preservado e nenhuma linha de erro adicional. Foram
19 amostras; não houve crescimento do total de erros após a rajada conhecida.
O spike anterior do PID 22488 continua com zero erros.

Três hosts XAML novos foram reportados entre 19:58:25 e 19:58:33, sem falha
observada. A memória privada variou de 122.408.960 a 220.880.896 bytes, e os
handles do processo de 4.368 a 6.900; a subida coincidiu com esses hosts.
Essas amostras não provam ausência de vazamento nem contam handles XAML vivos.

`reset` às 20:01:22 restaurou 198 elementos, manteve o PID e devolveu o visual
padrão. A configuração original (`theme` vazio, `logLevel` debug) foi
restaurada byte a byte, conferida por SHA-256. Evidências locais:
`final-stability.csv`, `final-smoke-log-before-rotation.txt`,
`final-reset.png` e `final-reset.log` no scratch do Plano 3b.

## O que falta fazer, em ordem

1. Resolver o **gate de pré-merge** abaixo: o repro adversarial com dumps e
   reinícios aguarda decisão explícita do dono da máquina. Não foi executado.
2. Com esse gate encerrado e validação completa, integrar o branch por
   fast-forward local, preservando o registro de decisões. **Nenhum merge
   agora; nunca commit direto em `main` nem push sem pedido do dono.**

Continuam parqueados, fora desta rodada: cache de `ResolveProperty`; extração
do cálculo do capturador mais próximo para teste puro; reaplicar somente a
propriedade alterada em vez do bucket inteiro; reduzir os headers de composição
incluídos por `winrt_common.h`. A guarda geral de valores negativos também não
foi adotada.

### Plano 4 — não iniciado

O app de bandeja em C# .NET 10: sempre ligado, sobrevive a reinício do explorer,
troca de tema pela bandeja. Ainda não tem plano escrito. Herda dois itens da
revisão final do Plano 3: o contador de handles VIVOS (especificação §7.2) e o
risco de deadlock em `SetSite(nullptr)` (`UnregisterWaitEx(INVALID_HANDLE_VALUE)`
mais o ramo `!site` ignorando `Deferred`).

## Questão em aberto: o reinício do explorer

Em 2026-09-14 às 14:13:13 o shell reiniciou, cerca de três minutos depois de um
smoke terminar em estado de reset, cem milissegundos após uma linha
`new XAML host` no log do TAP. Nenhum agente deu o comando.

**Causa indeterminada; não há dump que a atribua ou exclua o TAP.**

- Na investigação anterior não foram encontrados eventos "Application Error
  1000" correspondentes. Essa ausência não prova encerramento forçado nem
  exclui falha dentro da DLL.
- O evento "Winlogon 1002 — o shell parou" confirma a interrupção do shell;
  isoladamente não identifica o mecanismo nem o componente responsável.
- Houve quatro reinícios do shell naquele dia (09:20, 09:41, 13:48, 14:13),
  batendo com os reinícios do explorer feitos ao longo dos smokes para destravar
  a DLL na hora de relinkar. O das 14:13 é o último deles.
- A revisão da Task 4 não encontrou causa no diff; isso não substitui captura
  do erro em execução.
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

Na retomada foi confirmado por leitura que a chave `LocalDumps\\explorer.exe`
ainda não existe. A observação normal de dez minutos acima não substitui
esse ensaio adversarial com captura de dump.

## Pendências de teste manual

`docs/smoke-test.md` itens 7 a 9: segundo monitor; menu Iniciar e central de
notificações com tema aplicado; observação de dez minutos em repouso.
`held` é `unique_count - to_release.size()` **do lote drenado**, conforme
`src/tap/release_queue.cpp`; não é a contagem global de handles vivos. Portanto,
`held == elementos estilizados` e `held == 0` não provam o invariante global da
spec §7.2. Essa instrumentação continua pendente para o Plano 4; não marcar o
gate de handles vivos como validado com o contador atual.

## Onde estão as evidências que não estão no git

O diretório `.superpowers/sdd/2026-09-14-plano-3b-fidelidade/` tem um
`.gitignore` com `*`, então **nada dele está versionado**. Ele contém, na máquina
onde o trabalho foi feito: o registro de execução bruto, o relatório de cada task
com as evidências de teste, os diffs de cada revisão, e 22 capturas de tela dos
smokes anteriores (blur real com janela por baixo, claro e escuro, grão, pílulas
seguindo o rótulo, antes e depois do reset), além dos arquivos `t7-*` da
retomada. O que importa dessas evidências foi destilado nos registros de
decisões e do spike, que são versionados. Se precisar dos
originais e eles não existirem mais, não há como recuperá-los do git.

## Branches obsoletos

`plano-1-nucleo-e-temas`, `spike-pull-walk`, `spike-standing-crash` podem ser
apagados.
