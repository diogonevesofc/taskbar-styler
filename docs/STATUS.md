# Estado do trabalho

Atualizado em 2026-09-15. Este é o documento de passagem de bastão: onde o
trabalho parou, o que já está decidido e o que falta fazer, em ordem.

Leia antes: `AGENTS.md` na raiz (restrições invioláveis, como compilar e testar,
como o trabalho é organizado).

## Planos

| Plano | Escopo | Estado |
|---|---|---|
| 1 | `styler_core`: seletores, regras, temas; 55 temas convertidos byte a byte | mesclado em `main` |
| 2 | TAP carrega no explorer; exporta a árvore visual | mesclado em `main` |
| 3 | Aplicar e desfazer estilos; assinatura permanente; CLI `apply/reset` | mesclado em `main` |
| 3b | Fidelidade: blur real, variáveis de estilo, reciclagem, timeout no fan-out | concluído; gate operacional encerrado; integração local por fast-forward |
| 4 | App de bandeja em C# .NET 10, sempre ligado, sobrevive a restart do explorer | concluído e revisado; integração local por fast-forward |
| 5 | Catálogo visual com busca e prévia antes de aplicar | concluído e revisado; integração local por fast-forward |

`origin/main` permanece em `be0d3e6` e `origin/plano-3b-fidelidade` em
`0db9622`. A entrega local do Plano 3b inclui correções em `27ff1a8`,
documentação em `c2a3607`, correção do reset entre threads em `bfe330d` e o
registro final do gate. A integração em `main` usa fast-forward do branch
`plano-3b-fidelidade`, sem commit direto em `main` nem push.

## Plano 5 — entrega atual

Janela de personalização com busca por nome/autor, catálogo de 54 temas,
prévia offline e botões explícitos de aplicar/restaurar. A seleção e o fundo
claro/escuro da prévia não alteram o Windows. Fechar mantém a bandeja;
executar novamente reabre a mesma janela. Diagnóstico é acesso secundário.

Prévia ilustrativa derivada dos JSONs, com quatro layouts e limites explícitos.
Não é uma captura da taskbar nem substitui o teste visual do tema aplicado.
TAP inalterado. Pacote: `out/tray/TaskbarStyler.Tray.exe`.

Build/publish sem warnings, 58 testes C#, CTest core/TAP verdes e 23 verificações
de UI em harness STA; 100 trocas de prévia com GDI 53 → 53. Smoke real de busca,
prévia clara/escura, apply, reset e reabertura executado no Explorer PID 20712.
Config original DockLike restaurada byte a byte; nova janela deixada aberta.

Plano: [tarefas](superpowers/plans/2026-09-15-plano-5-interface-preview.md).
Evidências e limites: [decisões](superpowers/plano-5-decisoes.md).
Clique físico no ícone, matriz controlada de monitores/DPI/hot-plug e 24 h
continuam pendentes. O ledger desta sessão marcou `incomplete=1`; esta entrega
de UI não revalida o gate de cobertura do diagnóstico nativo. Sem push.

## Plano 4 — histórico da entrega

Plano: `docs/superpowers/plans/2026-09-14-plano-4-app-bandeja.md`.
Registro completo: `docs/superpowers/plano-4-decisoes.md`.

Entregues catálogo de 54 temas selecionáveis, config atômica compatível com
CLI, estados de falha/espera, diagnóstico, exportação segura e recuperação após
reinício. O reset deixa apenas o canal passivo de comandos. Retenção por owner,
callbacks antecipados e leitor simultâneo do log passaram por regressões e
smoke. Snapshot parcial preserva o arquivo anterior e aparece como falha.

Build/CTest verdes: core 129/7808, TAP 55/282; C# 33 testes, Python 22,
round-trip de 55 temas. DLL final SHA
`93DA4A2EFD9B493BC3631601D42228E45FB36072393AF43C00C8EF5D344101D2`.
Pacote local: `out/tray/TaskbarStyler.Tray.exe`, com .NET Desktop Runtime 10 x64.

O reinício final recuperou o tema sem intervenção (16356 → 5024). Exportação
inativa e saída concluídas; ledger observado `0/0/0`, visual e configuração
original restaurados. O caso negativo de árvore parcial registrou os erros
esperados e preservou SHA/mtime; não houve novo crash registrado no ensaio.

Próxima etapa: validação prolongada e matriz controlada de monitores/DPI/
hot-plug, sem declarar estabilidade de 24 h. O ledger não mede o cache privado
inteiro do XAML. O menu foi validado pela janela de diagnóstico; clique direto
no ícone continua pendente neste desktop (detalhes no registro). Não houve push
nem execução remota do CI.

## Plano 3b — histórico do fechamento

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

Validação mais recente (`bfe330d`): build completo com 0 warnings novos sob
`/W4`; **core 129/129** (7808 asserções), **tap 32/32** (109 asserções) e
**Python 22/22**, saída 0. A validação anterior confirmou os 55 temas iguais
byte a byte após extração, verificações de APIs proibidas e projeção WinUI sem
ocorrências, e includes WinRT concentrados em `winrt_common.h`. Smoke anterior
e 30 pares adversariais do novo artefato concluídos; observação final em reset
de **5min24s** também concluída, com PID preservado e zero erros registrados.

As implementações das Tasks 1 a 6 passaram por revisão independente e correções.
A revisão final encontrou os problemas adicionais descritos abaixo, já
corrigidos e re-revisados. O ensaio autorizado depois disso reproduziu um crash
e revelou uma lacuna no alcance do reset, corrigida e revisada em `bfe330d`.
O gate operacional foi encerrado após a reexecução, a observação final e a
restauração verificada das configurações. O resultado vale para o cenário
executado; não demonstra a causa exata do crash nem ausência universal de falhas.

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

### Gate autorizado — crash capturado, correção e novo ensaio

Após autorização explícita do dono, os dumps completos foram configurados às
20:08. O primeiro probe não gerou dump com `WerSvc` desabilitado; após mudar o
serviço para Manual e iniciá-lo, o probe PID 4692 produziu um dump completo
validado. As chaves e o estado anterior do serviço foram preservados.

O Explorer **PID 16800** falhou às 20:23 durante Task View após o terceiro reset:
três applies e três resets efetivos, mas apenas cinco fases completas do
harness; sete hosts novos, todos após reset, e zero `ERR` do TAP. O dump e o
evento 1000 identificam `0xC000027B` em `Windows.UI.Xaml.dll+0x90e383`, com
exceção armazenada `0x80070057` na thread **24196**. A pilha simbolizada chega
a `IDirectManipulationManager::Activate(m_hWnd)`; o HWND exato não foi
recuperado. Isso não atribui a falha ao blur nem estabelece a causa do incidente
histórico das 14:13. O primeiro ensaio reprovou o gate.

Foi confirmado um defeito independente: o reset enumerava hosts top-level
atuais, deixando de alcançar threads com estado após seus flyouts fecharem.
`bfe330d` cria um destino `HWND_MESSAGE` por thread, restaura por esses destinos
e impede trocar tema/sessão quando a restauração fica incompleta. Callbacks
não reaplicam o tema retirado durante o reset; a captura do valor original
continua acompanhando mudanças feitas pelo shell. Dois testes Win32 novos e
a revisão independente cobrem a correção. Sua relação causal com o crash
capturado permanece uma hipótese, não uma conclusão extraída do dump.

Nova DLL SHA-256:
`9EE5F1BAC8FE23F86C77B73F63F0E535C2833AB62871CCB5EF5E8E852EF25DE3`.
Task View foi exercitado em ambos os estados, com eventos adicionais de
abertura/fechamento de probes. Auditoria independente do log bruto confirmou:

| Sessão | PID | Intervalo dos 10 pares | Hosts em apply / reset | Thread principal / Task View |
|---|---|---|---|---|
| 101 | 25616 | 20:37:34–20:39:47 | 20 / 20 | 1868 / 17900 |
| 102 | 24572 | 20:40:15–20:42:26 | 21 / 20 | 8684 / 5676 |
| 103 | 25984 | 20:44:24–20:46:29 | 20 / 20 | 13484 / 10052 |

São **30 pares / 60 operações carregadas**, **121 hosts novos**, zero `ERR`,
zero falhas e zero fallbacks registrados. Cada uma das 60 fases tem novas
estatísticas na thread principal; cada um dos 57 reloads após a carga inicial
restaurou também a thread de Task View antes de parar a assinatura anterior.
Os resets seguintes à aplicação restauraram estado nessa thread; as
reaplicações após reset encontraram zero elementos para restaurar.

**Falha de pré-carga preservada:** na sessão 103, a primeira tentativa às
20:42:53 retornou `0x80070490 (ERROR_NOT_FOUND)`, antes de carregar o TAP.
A inspeção encontrou a DLL ausente e o PID 25984 estável. Uma nova tentativa
explícita no mesmo PID carregou via `VisualDiagConnection1` às 20:44:24 e só
então começaram os dez pares válidos. Portanto, houve **60 operações com TAP
carregado mais uma tentativa de pré-carga falha**. A causa exata dessa falha
de conexão não foi confirmada; ela não deve ser apagada do resultado nem
descrita como crash da nova DLL.

**Observação final concluída:** sessão 104, PID 25984, de **20:46:48.693 a
20:52:12.888**, total de **324,208 segundos**. As 22 amostras mantiveram o PID
e zero `ERR`. Task View foi acionado após 289,178 segundos, criando dois hosts
às 20:51:37.968 e 20:51:37.980; o acompanhamento continuou por cerca de 35
segundos. Total: **121 hosts nos 30 pares mais dois tardios, 123 ao todo**.
Essa janela não é o teste separado de dez minutos em repouso.

A conferência final às **20:56:48** não encontrou novo dump nem Application
Error 1000 do Explorer desde 20:35. Os três Winlogon 1002 desse
intervalo correspondem aos reinícios intencionais registrados antes dos comandos.

**Fechamento operacional concluído:** às 20:55:46, as duas chaves temporárias
de LocalDumps foram removidas e `WerSvc` voltou a Disabled/Stopped, como no
backup. Às 20:56:30, o config original (`theme` vazio, `logLevel` debug) foi
conferido byte a byte por SHA-256; o reset foi confirmado em ambas as threads,
o visual padrão foi capturado e nenhum probe permaneceu aberto. Dumps e
binários correspondentes foram preservados. **Gate encerrado neste cenário.**

Cronologia, hashes do artefato do crash, dump, controles sem TAP/com TAP vazio
e limites estão no [registro do repro](superpowers/plano-3b-repro-explorer.md).

## O que falta fazer, em ordem

1. Validar o clique direto no ícone da bandeja neste desktop; os comandos já
   foram exercitados pelo menu acessível na janela de diagnóstico.
2. Executar a matriz controlada de monitores, DPI e hot-plug, os cenários
   manuais abaixo e a observação prolongada antes de declarar esses requisitos
   validados. **Nunca push sem pedido explícito do dono.**

Continuam parqueados, fora desta rodada: cache de `ResolveProperty`; extração
do cálculo do capturador mais próximo para teste puro; reaplicar somente a
propriedade alterada em vez do bucket inteiro; reduzir os headers de composição
incluídos por `winrt_common.h`. A guarda geral de valores negativos também não
foi adotada.

### Plano 4 — entrega concluída

O app de bandeja em C# .NET 10 foi implementado e recuperou o tema após reinício
do Explorer no smoke. Os itens herdados da revisão final do Plano 3 foram
tratados: ledger de registros de handles observados e encerramento da assinatura
e dos recursos sem o ciclo de espera em `SetSite(nullptr)`. O alcance da contagem,
as evidências de execução e as limitações estão no
[registro de decisões](superpowers/plano-4-decisoes.md); as tarefas concluídas
estão no [plano](superpowers/plans/2026-09-14-plano-4-app-bandeja.md).

## Incidente histórico das 14:13 — causa ainda indeterminada

Em 2026-09-14 às 14:13:13 o shell reiniciou, cerca de três minutos depois de um
smoke terminar em estado de reset, cem milissegundos após uma linha
`new XAML host` no log do TAP. Nenhum agente deu o comando.

**Causa desse incidente indeterminada; não há dump das 14:13 que a atribua ou
exclua o TAP.** O crash capturado às 20:23 está registrado separadamente acima.

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

Esse incidente motivou o ensaio adversarial com LocalDumps, autorizado e
executado na rodada atual. A ausência inicial da chave de captura foi resolvida
às 20:08; houve uma falha real às 20:23, seguida da correção do alcance do reset
e do novo ensaio descrito acima. A proximidade de uma linha `new XAML host`
não demonstra, sozinha, o mecanismo da falha. No novo crash, o timestamp do
evento 1000 foi cerca de 348 ms depois da última linha; esse intervalo entre
registros não mede o instante exato da exceção.

## Pendências de validação ampliada

`docs/smoke-test.md` itens 7 a 9: segundo monitor; menu Iniciar e central de
notificações com tema aplicado; observação de dez minutos em repouso.
O Plano 4 substituiu a observação isolada do lote por um ledger por owner,
incluindo falhas e retenções entre lotes. `observed=0 incomplete=0 residual=0`
confirma a liberação dos registros observados; não prova o total privado de
handles do XAML. A matriz ampliada e a observação de 24 h continuam pendentes.

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
