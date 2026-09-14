# SDD ledger — plan: docs/superpowers/plans/2026-09-14-plano-3b-fidelidade.md

Branch: plano-3b-fidelidade (de main @ be0d3e6). Spec: docs/superpowers/specs/2026-09-12-taskbar-styler-design.md (lida).
Plano corrigido no pre-flight em bf08770 (3 edicoes, abaixo).

## Pre-flight (2026-09-14)

Pares que compartilham arquivo/interface:

| Par | Produz -> Consome | Achado |
|---|---|---|
| T2 -> T3 | `styler::BlurSpec`, `styler::BlurColor` (plano:371) -> `ToWinRtColor(const BlurColor&)`, campos dos efeitos | consistente (grep: BlurColor 9, BlurSpec 29) |
| T2 -> T4 | `PreparedStyle::blur` -> `ResolvedSetter::blur` (ponteiro para dentro do PreparedStyle; `t_cache_theme` segura o shared_ptr do tema, verificado em style_engine.cpp:215-230) | consistente |
| T3 -> T4 | 5 efeitos + `CreateNoiseStream` -> `MakeBlurBrush` | consistente (6 mencoes cada) |
| T4 <-> T6 | ambos editam property_setter.* e style_engine.cpp; regra T4 "brush de blur nunca no cache de setters" vs T6 "reaplicar dinamico por elemento" | sem cruzamento: 0 tags `<WindhawkBlur>` contem `{{` no corpus (grep) |
| T5 -> T6 | `ExpandStyleVariables`, `StyleVariableValue`, `FormatDoubleInvariant` | consistente (11 mencoes) |
| T6 -> T7 | `ForgetElementVariables(ElementId)` (plano:3087) -> chamado em T7 Step 1 | consistente |
| T2, T5, T6 -> matcher.h | campos novos `PreparedStyle::blur`, `::dynamic`, `PreparedRule::captures`, `ResolvedTheme::blur_specs` | nomes unicos, sequenciais |
| T7 -> engine | `t_state`, `RestoreElement(id, state)`, `OnElementAdded(id, element, nullptr)` | existem (style_engine.cpp:80,429,469); `reported_type` nulo vira L"" (linha 476) — verificar em T7 que `XamlElementView` cai em `get_class_name` com reported vazio |

Auto-consistencia por task: T1 testes puros (DescribeIncompleteTree x2) em tests/tap/test_tree_format.cpp (existe); T2 teste sobre as 272 tags do corpus; T5 11 doctest + caso de corpus; T3/T4/T6/T7 so runtime + smoke (aceito: nao ha XAML fora do explorer). Placeholders: 0. Testes que nao afirmam nada: nenhum mandado pelo plano.

## Rulings do pre-flight

- Ruling 1 (T1): `RunParam` no heap, `delete` so quando `SendMessageTimeoutW` devolve !=0, vazamento deliberado no timeout — porque apos timeout a mensagem pode seguir na fila e um hook ja iniciado continua apos `UnhookWindowsHookEx` (doc do UnhookWindowsHookEx); os 4 chamadores passam `param=nullptr` e o hook le `p` so na entrada, mas a consequencia e chamar ponteiro lixo no explorer — custo se errado: sizeof(RunParam) por timeout, cada um logado como Error.
- Ruling 2 (fatos): citacao `vendor:13831-13842` -> `13843-13854` (`ShouldUseFallback` verificado em 13843; a afirmacao "retorna false sem FallbackColor" esta certa, a decisao de cortar a troca por economia de energia fica) — custo se errado: nenhum.
- Ruling 3 (T7): o overload `FindElementId(wf::IInspectable const&)` obtem o handle como o upstream `HandleFromInspectable` (vendor:10961: QI por IInspectable, ponteiro = handle), nao `get_abi(FrameworkElement)` — custo se errado: lookup falha e a reaplicacao vira no-op, visivel no spike da T7.
- Ruling 4 (processo): revisao por task em T1, T3, T4, T6 (handles/COM/threads); T2 e T5 sao puras com testes — revisao por task mantida mas em modelo mais barato; T7 e spike-gated e provavelmente cortada.

## Execucao

Task 1: BASE bf08770, implementer sonnet despachado 2026-09-14.
Task 1: implementer DONE em 573b8ca (core 106/106, tap 23/23). Nota operacional do implementer: 'cmd /c' pela tool Bash nao executa neste ambiente; usar a tool PowerShell para VsDevCmd/cmake/ctest. Explorer reiniciado uma vez (LNK1168, DLL carregada). Review opus despachada.
Task 1 review (opus): spec OK; 3 Important, todos plan-mandated: (a) GetLastError lido depois de UnhookWindowsHookEx; (b) DescribeIncompleteTree conta filho duplicado duas vezes em 'delivered' (silencia truncamento real); (c) header de RunOnWindowThread nao documenta o contrato novo (false = nao confirmado, proc pode rodar depois, param nao pode ser memoria do chamador). Ruling 5: aceitos os tres contra o texto do plano — o plano errou; custo se errado: nenhum (sao correcoes locais). Minor 'reload: taskbar UI window not found' em theme_session.cpp:117 fica para a task que tocar esse arquivo (T2 ou T6). Fix round 1: resume implementer sonnet.
Task 1 fix round 1: b980ba5 (tap 24/24, core 106/106). Re-review sonnet despachada.
Task 1: complete — re-review: 3/3 ADDRESSED, sem quebra nova. HEAD b980ba5.

Task 2: BASE b980ba5, implementer sonnet despachado 2026-09-14. Inclui o Minor herdado da review da T1 (theme_session.cpp:117 loga 'UI window not found' tambem no timeout).
Task 2: implementer DONE_WITH_CONCERNS em 978fd63 (core 114/114, tap 24/24). Divergencias do brief: contagem por constante distinta (pseudocodigo do plano dava 439), corpus 269 e nao 272 (8 temas com typo '<<WindhawkBlur'), test_matcher.cpp ajustado. Review opus despachada com os pontos nomeados.
Task 2: complete — review opus Approved, 0 Critical/Important. Ruling 6: as 3 divergencias do brief sao 'brief-defect, implementer correct' (contagem por constante distinta; 269 = 272 grep - 8 typos '<<WindhawkBlur' + 5 alias; test_matcher.cpp ajustado). Minors PARQUEADOS para a T5 (toca matcher): (a) test_blur.cpp:124-127 lista 4 alias, sao 5 (falta FrostedAcrylic); (b) tres convencoes de trim (blur.cpp Trim, blur_rewrite.cpp Trim, matcher.cpp:280) — valor com newline inicial parseia mas nao e reescrito, flag 'rewritten' descartada em matcher.cpp:267; (c) prefix matcher duplicado blur.cpp:233 / blur_rewrite.cpp:46; (d) matcher.h:47 comentario de blur_specs (= fontes distintas, nao estilos); (e) includes nao usados em blur.cpp (<array>,<stdexcept>) e test_blur.cpp (<fstream>,<nlohmann/json.hpp>), falta <vector> em blur.cpp. Para a T4: ler PreparedStyle::blur, nunca o contador. HEAD 978fd63.

Task 3: BASE 978fd63, implementer sonnet despachado 2026-09-14.
Task 3: implementer interrompido por limite de sessao no rebuild (4 arquivos na arvore, sem commit). Retomado o mesmo agente 2026-09-14.
Task 3: implementer DONE em 943a5c8 (core 114/114, tap 24/24). Divergencia: #define INITGUID em blur_effects.cpp (CLSID_D2D1* dava LNK2001; o probe do plano nunca linkou). Review opus despachada.
Task 3 review (opus): spec OK byte-fiel; INITGUID aceito (Ruling 7: a premissa 'segundo TU = LNK2005' que eu dei ao revisor estava errada — DEFINE_GUID sob INITGUID e DECLSPEC_SELECTANY, o linker funde; comentario do implementer esta certo; dxguid.lib seria a alternativa de uma linha). 2 Important, ambos brief-defect: (a) AlphaMode mapeado COLORMATRIX_ALPHA_MODE em vez de DIRECT (vendor:13257); (b) m_name vazio nos 5 efeitos, upstream usa o nome do tipo e a T4 deixa Border e Composite-raiz no default -> dois efeitos '' no grafo. Ruling 8: corrigir os dois + Minors 2 (out-param so no match) e 4 (guard NaN em density, wcstod aceita 'nan'). Fix round 1: resume implementer sonnet. Minors restantes (copy_to_abi, StoreAsync status, includes, custo de compilacao do winrt_common.h) ficam para a review final.
Task 3 fix round 1: 6c68ec6 (core 114/114, tap 24/24). Re-review sonnet despachada.
Task 3: complete — re-review 4/4 ADDRESSED, sem quebra nova. HEAD 6c68ec6.

Task 4: BASE 6c68ec6, implementer opus despachado 2026-09-14 (smoke ao vivo obrigatorio: log + PID + screenshot; termina em reset).
Task 4: agente morreu com o fim da sessao apos o smoke (11 PNGs, codigo na arvore, sem report/commit). Retomado o mesmo agente 2026-09-14. Push feito pelo usuario: main be0d3e6 e plano-3b-fidelidade (ate 6c68ec6) em origin.
Task 4: implementer DONE_WITH_CONCERNS em 857a493 (core 114/114, tap 24/24, 0 warnings novos). Smoke: TranslucentTaskbar 2 brushes/0 fallback, Command_Center 49/0, Luminosity 2-3/0, reset restored 36 held 0, 0 ERR, PID do explorer estavel DURANTE o ciclo, blur visivel. Concerns do implementer: (1) grao invisivel em NoiseOpacity 0.1 (n^2, igual upstream); (2) linha Debug em RefreshBrush (o codigo do brief nao emitia a linha por rebuild que o proprio ciclo de teste exige); (3) claro/escuro por script precisa de WM_SETTINGCHANGE; (4) explorer reiniciou 14:13:13 ~3min apos o smoke. INVESTIGADO: Winlogon 1002 'o shell parou repentinamente', 100ms apos 'new XAML host' no log; SEM WER de explorer e SEM Application Error 1000 hoje -> sem dump provando falha na nossa DLL. Registrado em docs/STATUS.md 'Em investigacao'. Review opus despachada com o crash como risco nomeado.

Task 4 review (opus): spec/cache-isolation/restore/guards/thread-affinity todos CLEAN, port fiel ao upstream (screenshots conferem blur real, nao tint). 0 Critical. 2 Minor (doc item 12 exagera o grao; comentario blur_brush.cpp:349 contradiz a medicao) -> fix round 1 despachado. 1 Important: o crash 14:13 'unproven, needs repro'.
CRASH INVESTIGADO (event log; interpretação corrigida na retomada Codex): não foram encontrados Application Error 1000 correspondentes. Às 14:13:13 há Winlogon 1002 ('shell parou'); houve outros três registros às 09:20, 09:41 e 13:48, em um dia com reinícios feitos durante smokes para liberar LNK1168. Esses dados não provam kill/exit nem excluem falha do TAP. A causa permanece INDETERMINADA, sem dump que identifique o componente responsável. A hipótese anterior de cleanup do agente não foi comprovada. Repro adversarial (apply Command_Center <-> reset com LocalDumps armado e criação de hosts) continua aguardando decisão do usuário por envolver configuração global de dumps e reinícios repetidos do Explorer.
Task 4 fix round 1: b080abc (doc+comentario apenas, conferido pelo controller — sem re-review de agente por ser doc/comment trivial; core 114/114, tap 24/24). Codigo da T4 + os 2 Minor: FECHADOS. Item Important ABERTO como gate de pre-merge: repro do crash, decisao do usuario pendente (opcoes dadas). T4 nao marcada 'complete' ate isso.

Task 5: BASE b080abc, implementer sonnet despachado 2026-09-14 EM PARALELO (pura, core, sem runtime/explorer — independente da questao do crash). Inclui os Minor parqueados da review da T2.
Task 5: implementer DONE em 2353759 (core 126/126: 114 + 11 novos + 1 corpus; tap 24/24; 5 cleanups feitos). Concerns: test_matcher.cpp mudou fora da lista (rename skipped_dynamic + comportamento keep-dynamic); {{clickThroughTaskbar}} do LiquidGlass2 e ref a setting do mod nunca capturada (skip esperado, igual {{__unset}} de Pills). Mediu 110 valores dinamicos / 107 expand / 3 skip — VERIFICAR contra os '168 referencias' da tabela de fatos do plano. Review opus despachada.
Task 5 review (opus): spec/fail-closed/FormatDouble/contagem/5 cleanups CLEAN. Contagem reconciliada: 170 raw {{ = 168 refs (em style values) + 2 {{__unset}} em constantes; 110 = styles dinamicos distintos (muitos values tem varios {{}}); 110 = 107 expand + 3 skip (2 __unset + 1 clickThroughTaskbar). Mesma distincao ref-vs-value do 269-vs-272; assercao ancorada em failures.empty(), nao no numero. 1 Important: ternario NAO faz curto-circuito (avalia os dois ramos) — diverge do upstream (vendor:16330), sobre-pula (guarda x==0?0:100/x) e sobre-captura deps; fail-safe mas a T6 liga isso. Ruling 9: corrigir antes da T6 (deps importam la). Minor (trim alargado p/ CR/LF) aceito. Fix round 1: resume implementer sonnet.
Task 5 fix round 1: fe3b95b (core 128/128, 2 testes novos de curto-circuito RED/GREEN; contagem 110/107/3 inalterada — bug era latente). Re-review sonnet despachada.
Task 5: complete — re-review ADDRESSED, sem quebra, ternario line-for-line com vendor:16335. HEAD fe3b95b.

Task 6: BASE fe3b95b, implementer opus despachado 2026-09-14 (muda pixel; smoke ao vivo; observar estabilidade do explorer — alimenta a questao do crash da T4).
Task 6: implementer interrompido por limite de sessao (style_variables.h/.cpp criados, 6 arquivos modificados, sem CMake/log/docs, sem build, sem commit). Retomado o mesmo agente.
Task 6: implementer DONE_WITH_CONCERNS em 844a4b7 (core 129/129, tap 24/24, 0 warnings novos). Smoke: Pills 7 captures/51 dinamicos, 185 held == 185 elements, 6 variaveis; Blob 3/31. 2 ERR = {ThemeResource Default} invalido do proprio Blob.json (confirmado por mim: 1 ocorrencia no tema; Pills usa {{__unset}} certo) — bug de conteudo de tema, caminho estatico, NAO regressao. unresolved 17-19 na enxurrada -> 0 em regime (prova do SizeChanged). reset -> restored 185, depois restored 0. Concerns: ForgetElementVariables no topo de OnElementRemoved (desvio deliberado); RegisterCapture depende do try/catch do chamador.
EVIDENCIA DO CRASH (T4): PID 23624 do explorer ESTAVEL durante todo o smoke longo da T6, 0 'new XAML host', 0 crash. Esse resultado não reproduz nem explica o reinício das 14:13; a causa continua indeterminada. Review opus da T6 despachada.
Task 6 review (opus): spec completa; desvio do ForgetElementVariables julgado CORRETO (brief-defect); Blob = conteudo de tema pre-existente. 3 Important de tempo de vida de estado: (1) ReapplyCallback() e static global escrito por toda thread -> race (fix: thread_local); (2) LookupForConsumer pontua sites mortos -> chain de ponteiros liberados pode vencer o contest e servir numero congelado (fix: checar weak_ref); (3) dynamic_styles guarda PreparedStyle* que pode sobreviver ao tema numa thread so-dinamica (t_cache_theme so e setado em CachedSetter) -> deref pos-SetTheme(nullptr) (fix: setar t_cache_theme incondicional em OnElementAdded). + lacuna de evidencia: o smoke NAO prova o caminho SizeChanged (o unresolved 17-19->0 se explica por ordem de relatorio + PropagateChange do RegisterCapture; 0 failed prova que BtnW ja era pos-layout no seed). Ruling 10: corrigir os 3 + Minors 'leak de CaptureSite' e 're-report loga Error' (amarrados) + endurecer ReapplyDynamicProperty (nao segurar refs atraves de XamlReader.Load — MESMA classe de bug do crash do Plano 3); fechar a lacuna com smoke de hover no Pills. PARQUEADOS p/ review final: cache de ResolveProperty, tornar o scoring testavel, ApplyBucketForState reaplicar bucket inteiro. Fix round 1 despachado.
Task 6 fix round 1: 127e8fe (core 129/129, tap 24/24, 0 warnings novos). 6/6 fixes aplicados. LACUNA FECHADA: forcou expansao de rotulo via TaskbarGlomLevel 0->2 com WM_SETTINGCHANGE (restaurado); pilhas 40px -> ate 280px, cada uma seguindo a largura do proprio rotulo, e voltando — prova o SizeChanged nos dois sentidos, sem reaplicar tema. 2 'new XAML host' com thread nova = a condicao multi-thread que o fix 1 enderecava. PID mudou so uma vez, por LNK1168 (restart autorizado pelo proprio agente), nao crash.
ACHADO NOVO (nao corrigido): 136 ERR 0x80070057 numa rajada de 777ms na transicao de crescimento — {{BtnW-6}} vira -6 enquanto ActualWidth passa por zero no rebuild do painel; XAML rejeita MinWidth negativo. Autocuravel, ja contado em failed_styles, sem efeito visual; direcao de encolhimento deu 0 ERR. Ruling 11: NAO adicionar guarda de valor negativo agora (exigiria whitelist de propriedades nao-negativas = scope creep e divergencia do upstream). Em vez disso, apos a re-review: (a) incluir o NOME DA PROPRIEDADE na linha de erro de ApplyBucketForState (pedido do implementer, melhora todo diagnostico futuro) e (b) documentar a rajada transitoria no smoke-test.md para o criterio '0 ERR' continuar significando algo. Levar a decisao da guarda para a review final. Re-review sonnet despachada.
Task 6: COMPLETE — re-review 6/6 ADDRESSED, sem quebra nova; lacuna de evidencia FECHADA (o glom-level test prova SizeChanged -> SetCaptureValue -> PropagateChange -> ReapplyDynamicProperty ao vivo, larguras proporcionais ao rotulo de cada botao, reversivel, PID constante). AdoptTheme byte-identico ao logica antiga, so fatorado e chamado antes. HEAD 127e8fe.

PARADA A PEDIDO DO USUARIO: 'depois dessa task, pare tudo e documente tudo que foi feito e que precisar ser feito ainda que eu vou continuar no Codex.' Task 7 NAO iniciada. Nenhuma mudanca de codigo alem do que ja foi revisado. Handoff escrito em docs/STATUS.md e docs/superpowers/plano-3b-decisoes.md (este ledger, preservado).

## Retomada no Codex — 2026-09-14

O parágrafo de parada acima descreve o handoff histórico. Na retomada, `HEAD`
e `origin/plano-3b-fidelidade` foram conferidos em `0db9622`: o handoff já havia
sido enviado ao remoto. As mudanças abaixo ainda estão na árvore de trabalho,
sem commit, merge ou push desta rodada.

### Decisão 12 — diagnóstico da rajada transitória

Cumpridos os dois ajustes da decisão 11: `ApplyBucketForState` inclui o nome
da propriedade no erro; o smoke documenta a rajada `MinWidth`/`{{BtnW-6}}`
durante rebuild. A exceção exige causa identificada, recuperação visual e
ausência de erros após estabilizar o layout. Os 136 erros em 777 ms são a
medição histórica, não um limite permissivo. Não foi adicionada guarda geral
de valores negativos.

### Decisão 13 — Task 7 encerrada pelo spike, sem código adicional

Resultado: **sem defeito observável neste cenário**, sem concluir que a taskbar
não recicla. O snapshot inicial de 19:38:03 tinha duas árvores de 12 botões e
24 linhas de estilização. Helpers Win32 com AUMIDs distintos permitiram abrir
B/C/D/E/F/G, fechar C/D/E e abrir H/I/J. Não houve novos `styled` dos botões
raiz durante as trocas; reaplicar `Pills` às 19:46:35 produziu 24 novas linhas,
com resultado visual igual nas capturas antes/depois. Isso mostra a limitação
do critério "uma linha por novo aplicativo" diante de botões pré-criados.

PID 22488 estável; 0 `ERR` desse processo. As linhas `ERR x` do arquivo vieram
dos testes PID 21996/24768. Reset às 19:47:26 restaurou 223 elementos e o lote
seguinte registrou `0 held`. Ícones agrupados, sem rótulos, captura de um
monitor: a conclusão não se estende a outros temas/configurações/versões.
Procedimento e evidências em [plano-3b-spike-reciclagem.md](plano-3b-spike-reciclagem.md).

### Decisão 14 — dependências e tempo de vida dos estilos dinâmicos

A revisão final encontrou perda de dependências: a inscrição era por
`(elemento, propriedade)`, mas cada estilo/estado substituía o registro do
anterior. Uma regra já derrotada também expandia antes do teste de precedência
e podia sobrescrever a inscrição vencedora. Uma linha estática posterior no
mesmo estado deixava o template dinâmico anterior ativo.

Correção: filtrar propriedade já reivindicada e estado sem grupo antes da
expansão; manter template e dependências por estado efetivo; publicar uma
união deduplicada por propriedade, tanto na construção quanto na reexpansão.
Estática posterior elimina template/dependências daquele estado. Falha de
parsing XAML preserva template/dependências para retry. **Mudança de
comportamento deliberada:** na reexpansão, o valor inválido é retirado e cai no
estado padrão ou `Unapply`; antes a exceção mantinha o último valor bom.

A revisão também encontrou que copiar `PreparedStyle*` não mantinha o tema
vivo durante `XamlReader.Load`: uma recarga reentrante podia liberar os
templates antes da próxima iteração. Agora há `shared_ptr` local durante a
resolução e checagem da geração do elemento/identidade do tema antes de
publicar resultados. Na construção inicial, há a mesma checagem antes de
reutilizar referências após resolver XAML.

Quatro testes novos usam o helper puro e o avaliador real: união entre estados,
mudança de ramo condicional, substituição dinâmica por estática e variável
inicialmente indefinida. Eles não executam `XamlReader`, reentrância do Explorer
nem a precedência entre regras dentro do motor. Re-revisão estática escopada
concluída, inclusive dos guards finais, sem novos achados verificáveis.

### Decisão 15 — interface do efeito e armazenamento do ruído

`DetachSource` convertia o `IUnknown` canônico para `IGraphicsEffectSource`.
O teste COM confirmou endereços distintos para essas interfaces nos efeitos;
o retorno agora usa `copy_to_abi` da interface correta, com destino local nulo.

O armazenamento do bitmap passa por `GetResults()` também quando já terminou,
propagando erro/cancelamento em vez de cachear resultado inválido. Se
`StoreAsync` ainda estiver `Started`, cancela e lança `E_PENDING` para o
fallback existente; não há espera bloqueante na thread de UI. Dois testes
novos verificam a identidade da interface e o bitmap completo em STA, incluindo
cursores independentes dos clones.

A primeira execução dos testes teve AV ao encerrar o processo: a fixture
desinicializava COM antes da destruição do stream em cache `thread_local`.
A fixture agora mantém o apartment vivo até depois desse cache. A suíte inteira
passou com saída 0 após a correção. Essa AV do runner não comprova a causa do
reinício do Explorer às 14:13; o gate histórico permanece aberto.

### Validação e pendências desta rodada

Build completo `/W4` sem novos warnings; `ctest --verbose` com core **129/129,
7808 asserções** e TAP **30/30, 92 asserções**, saída 0. Extração dos 55 temas
igual byte a byte; buscas de APIs proibidas/projeção WinUI sem ocorrências;
WinRT incluído somente pelo header central. `pytest tools/`: 22 verdes antes
da última recompilação. Correções consolidadas em `27ff1a8`; smoke concluído
com a exceção transitória detalhada abaixo.

Smoke da nova DLL, SHA-256
`E7B90B57D2B566E1ACD7EC0F5CADED8A40B4378C56F17F74AA1998E1B442724A`:
Explorer PID 19132 desde o restart autorizado às 19:47:50. Aplicações:
Pills 19:49:00 (157 elementos/664 propriedades/36 brushes/6 variáveis),
Command_Center 19:50:00 (66/268/52 brushes/0 fallbacks), TranslucentTaskbar
19:50:03 e Pills 19:50:06 (155/638/34 brushes/6 variáveis).

Ao abrir três janelas houve 28 `ERR 0x80070057` de 19:52:12.226 a
19:52:12.730, duração 504 ms. O novo diagnóstico confirmou `MinWidth` **e**
`MaxWidth`, ambas com `{{BtnW-6}}` no tema Pills. A exceção do smoke foi
expandida somente para essas duas propriedades e essa causa. Captura
`final-pills-new-buttons.png` visualmente correta; fechamento das janelas sem
nova rajada. O monitor inicial parou ao detectar os erros e foi retomado para
verificar ausência de novas linhas depois das 28 conhecidas. A observação
total de dez minutos terminou às 20:00:44 (19 amostras, início às 19:50:44),
sem novo erro e com o mesmo PID; não declarar zero erros totais.

Houve três `new XAML host` às 19:58:25/28/33. Memória privada entre
122.408.960 e 220.880.896 bytes; handles do processo entre 4.368 e 6.900,
com aumento no intervalo desses hosts. Sem atribuição causal ou alegação de
ausência de vazamento. Reset às 20:01:22: 198 elementos restaurados, visual
padrão confirmado e PID preservado. `config.json` voltou byte a byte ao
original, comparado por SHA-256. CSV e capturas `final-*` preservados no scratch.

**Retificação do contador:** `held` é calculado por lote drenado, não acumula
todos os handles vivos. Os registros históricos comparando `held` com
`elements`, ou usando `held == 0` após reset, não provam o invariante global
da spec §7.2. `docs/smoke-test.md` foi corrigido; a instrumentação global segue
pendente para o Plano 4.

Permanecem fora desta rodada: cache de `ResolveProperty`, cálculo do capturador
mais próximo extraído para teste puro, reaplicação restrita à propriedade
alterada e redução dos headers de composição. A guarda geral de negativos não
foi adotada.

**Gate de pré-merge ABERTO:** repro adversarial com dumps/reinícios e criação de
hosts XAML ainda depende da decisão explícita do dono da máquina. A ausência de
Event 1000 não encerra essa investigação. Sem merge e sem início do Plano 4.
