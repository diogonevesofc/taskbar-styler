# SDD ledger — plan: docs/superpowers/plans/2026-09-13-plano-3-aplicar-e-desfazer-estilos.md

Branch: plano-3-aplicar-estilos (de main em 8537aa7, que ja contem os Planos 1 e 2).
Spec: docs/superpowers/specs/2026-09-12-taskbar-styler-design.md.
Modo aprovado pelo usuario no Plano 2 e mantido: revisao por task nas tasks que
mexem com handle, callback ou tempo de vida COM dentro do explorer (4, 5, 6);
tasks 1, 2, 3, 7 e 8 podem ir em lote/sem review por task se o ritmo pedir,
com a revisao final do branch cobrindo tudo.

## Varredura pre-voo

| Tasks | Compartilham | Produz -> consome | Achado |
|---|---|---|---|
| T1,T4,T5,T7,T8 | src/tap/tap_boundary.cpp (SetSite) | cada uma acrescenta depois do bloco anterior: export -> ProbeWinRt -> LoadConfiguredTheme -> StartSubscription -> StartReloadWatch | OK, ordem fixada no plano |
| T1 -> T5/T7 | InitializationData() | T1 le GetInitializationData em OpenDiagnostics; T5 usa em LoadConfiguredTheme | OK |
| T1 -> T7 | LoadTap(pid, tap, init_data), ThemesDir() | T7 CmdApply chama CmdLoad | OK |
| T2 -> T3 | ResolveConstants, ApplyStyleConstants, AdjustTypeName, RewriteWindhawkBlur | assinaturas identicas nas duas tasks | OK |
| T3 -> T5/T6 | PreparedStyle, PreparedRule, ResolvedTheme, RuleMatch, ElementView | T5 XamlElementView implementa a interface; T6 le RuleMatch::vsg | OK |
| T4 -> T5 | style_engine.h | T4 declara o stub com 3 funcoes; T5 SUBSTITUI o header mantendo as 3 | OK, documentado na T5 Step 4 |
| T4 -> T5/T6 | ElementState em style_engine.cpp | T5 define; T6 redefine (buckets) so nesse arquivo | OK |
| T4 -> T4 | release_queue.cpp usa ElementHasState/FindElementId/ForgetElementIdIfDead/ReapDeadElementIdsIfNeeded; element_registry.cpp chama OnElementRemoved | todos declarados nos headers da propria T4 | OK |
| T5 -> T7 | ConfigPath() | T5 define em theme_session; T7 MOVE para ipc.h | CONFLITO POR DESENHO, ver Ruling 1 |
| T5 -> T8 | OnElementAdded | T6 reescreve; T8 insere MergeResourceVariablesForThisThread no inicio | OK, T8 edita a versao da T6 |
| T4 -> T8 | log "drained N handles, M held" | M = pending_count - to_release.size() | OK, ambos os trechos no plano |
| T1 -> CI | grep de um unico include site de winrt/ | T8 acrescenta includes so dentro de winrt_common.h | OK |
| T6 | event_token de CurrentStateChanged guardado como long long | winrt::event_token{valor} reconstroi | OK |

Consistencia interna por task: T1 (LoadTap muda em loader.h, loader.cpp e main.cpp; test_loader.cpp acompanha) OK; T2 (teste de corpus usa ordered_json e linka nlohmann no alvo de teste) OK; T3 (teste constroi ValueRule via ParseStyleRule; nota no plano se {{ }} nao parsear) OK; T4 (callback heap com refcount; Subscription heap-leaked) OK; T5 (t_modifying protege reentrancia; callback captura so id) OK; T6 (buckets em vector nunca reordenado; indices capturados) OK; T7 (StartReloadWatch antes do StopSubscription no !site) OK; T8 (Unmerge chamado em RestoreAllOnThisThread) OK.

Ruling 1 — ConfigPath() nasce em theme_session.cpp (T5) e migra para ipc.h (T7).
Por que: a T5 precisa dele para o smoke e a T7 e quem cria o ipc.h que o CLI
compartilha. O despacho da T7 carrega a instrucao explicita de remover a
definicao da T5. Custo se errado: definicao duplicada = erro de link, pego no
build da T7.

Task 1 despachada (sonnet). BASE = 8537aa7.
Nota de sequenciamento: as Tasks 2 e 3 sao puras (src/core, tests/core) e nao
tocam nos arquivos da Task 1, mas dividem o mesmo working tree e o mesmo index
do git — dois implementadores commitando ao mesmo tempo no mesmo checkout se
atropelam (foi por isso que no Plano 2 o fix da Task 5 esperou o lote 6+7).
Sequencial, como a skill manda. Briefs 2 e 3 extraidos adiantado.

Task 1: complete (commits 8537aa7..5b99780, review por task dispensada — modo
aprovado pelo usuario para tasks sem handle/COM lifetime; a revisao final cobre).
ctest 2/2; smoke ao vivo: "init data: ...\build\src\cli\themes" e "winrt ok:
Windows.UI.Xaml.Controls.Grid"; tap_boundary.cpp compila em 17.0 s.
Task 1: minor (deferred): test_log.cpp escreve linhas "x" no log REAL em
%LOCALAPPDATA% a cada ctest — pre-existente do Plano 2, nao e regressao; a
revisao final decide se vale sandboxar.
Task 1: minor (deferred): ramo de aviso de ThemesDir() vazio nao exercitado
(a copia POST_BUILD sempre deixa themes/ ao lado do exe).

Tasks 2+3 despachadas em LOTE para um implementador (sonnet), dois commits
separados. Ambas sao puras (src/core + tests/core), a 3 consome a 2, e um unico
contexto evita reconfigurar o ambiente de build duas vezes. BASE = 5b99780.

Tasks 2+3: DONE_WITH_CONCERNS. Commits 4535fc6 (Task 2) e 1fdfaa3 (Task 3).
ctest: 95/96 casos (7615/7616 asserts); a unica falha e o teste do conjunto de
"$ nao resolvido" no corpus. A equivalencia dos 55 temas com a implementacao
independente da ordem do upstream PASSOU — essa e a prova que importa.

Ruling 2 — o conjunto esperado de temas com "$" nao resolvido e {Fluid}, nao os
tres da spec §7.6. Meu erro, de novo por medicao com algoritmo errado: no Plano 1
medi com casamento EXATO de nome; o upstream casa por PREFIXO, nome mais longo
primeiro. $WidgetGap57 nos dois Luminosity resolve como <WidgetGap> + "57",
exatamente como no upstream. So o Fluid exercita o passthrough do "$" solto.
Implementador retomado para corrigir o teste e a frase da spec (commit de
follow-up). Custo se errado: nenhum em comportamento — o algoritmo e o do
upstream, provado por equivalencia nos 55 temas; muda so o que o teste afirma.

Ruling 3 — reescrever o WindhawkBlur nas constantes resolvidas dentro de
PrepareTheme (copia local; ResolveConstants e seu contrato de corpus intactos)
e aceito. O plano so reescrevia nos estilos e o proprio teste do plano esperava
a variavel de recurso reescrita — defeito do plano, pego pelo implementador. O
mapeamento interino vale onde quer que um blur apareca. Custo se errado: o
Plano 3b, ao trazer o WindhawkBlur real, remove a reescrita num lugar so.

Tasks 2+3: follow-up d004f41 — teste corrigido para {Fluid}, spec §7.6 corrigida.
ctest core: 96/96 casos, 7616/7616 asserts.
Task 2: complete (commits 5b99780..4535fc6 + d004f41, review por task dispensada)
Task 3: complete (commits 4535fc6..1fdfaa3 + d004f41, review por task dispensada)
Nota para a Task 5 (log do tema): blur_approximations conta FONTES reescritas
(uma por constante com blur, mais uma por estilo inline), nao usos. No corpus:
41 constantes cobrem 142 usos, mais 223 inline. A linha de log da Task 5 deve
dizer "N valores de blur aproximados", nunca "N estilos".

Task 4 despachada (sonnet). BASE = d004f41. Mantem review por task (handles,
callback dentro da varredura do XAML, refcount COM).

Task 4: implementador (sonnet) derrubado pelo limite de sessao do usuario antes
de commitar (tinha so lido o contexto). Limite reiniciado; agente RETOMADO no
mesmo contexto com instrucao de conferir git status antes de escrever, para nao
recomecar do zero o que ja estiver no working tree. BASE continua d004f41.

Task 4: DONE_WITH_CONCERNS, commit b919419 — e o concern e BLOQUEANTE:
a assinatura permanente DERRUBA o explorer, reproduzivel (2/2), dentro do
AdviseVisualTreeChange (lote inicial sincrono), antes de a chamada retornar.
Em Info (sem log por elemento): snapshot do Plano 2 OK (495 elementos), winrt ok,
depois silencio; "subscription started" nunca aparece; ~2 s depois Winlogon 1002
"shell stopped abruptly". Em Debug: 76 "add" e a mesma morte. Event log mostra
access violation em SystemTray.dll. ctest 2/2 (3 casos novos de release_policy).
Hipotese do implementador (nao confirmada): GetIInspectableFromHandle chamado
reentrante de dentro da varredura do Advise.
MAS o upstream faz exatamente isso (FromHandle dentro de OnVisualTreeChange,
vendor:11121) e nao cai. Diferencas estruturais nossas: (a) o SetSite ja fez UMA
assinatura (snapshot) e liberou TODOS os handles via UnregisterInstance antes de
assinar de novo; (b) ProbeWinRt chamou GetUiLayer antes; (c) o stub loga
get_class_name por elemento. Nao vou rular por teoria (memoria
verify-api-facts-before-ruling): spike com tres experimentos discriminantes.
Task 4 NAO vai para review enquanto derruba o shell.

Spike da queda (branch spike-standing-crash, 5 commits): E1 sem snapshot e sem
probe = CAIU (diferencas 1 e 2 descartadas); E2 lookup adiado para depois do
Advise = o Advise COMPLETOU (495 adds) e caiu no laco simples de resolucao
(reentrancia descartada); E3 = travou; E4 fix do registro + sem get_class_name
= TRAVOU em vez de cair, parando logo depois de "add 72 (SystemTray.StackListView)",
o mesmo ponto em que o baseline morria em 76; E5 Advise fora do SetSite = igual.
Conclusao do spike: causa NAO isolada; todas as quatro candidatas excluidas. O
que esta isolado e o ponto: resolver o objeto (GetIInspectableFromHandle) do
elemento seguinte ao SystemTray.StackListView#IconStack mata/trava, em qualquer
configuracao; o snapshot do Plano 2 percorre os 509 sem resolver e vive.
Diferenca de codigo restante no caminho: InspectableFromRaw faz attach + QI
extra (.as<IInspectable>); o upstream escreve direto em put_abi, sem QI.
Proximo passo (sem rular por teoria): E6 = InspectableFromRaw sem o QI; se ainda
falhar, E7 = pular a resolucao de elementos cujo Type reportado comeca com
"SystemTray." para saber se e UM elemento venenoso ou uma classe. O fix do
registro (E4, 9a3c6d0) entra de qualquer jeito: referencia em unordered_map
atravessando make_weak e use-after-free esperando reentrancia.

CAUSA ENCONTRADA (spike E6/E7 + leitura minha de vendor:10904-10914, 11013-11030):
E6 (InspectableFromRaw sem QI) CAIU — hipotese refutada. E7 logando o tipo ANTES
de resolver mostrou o elemento assassino: Windows.UI.Composition.SpriteVisual —
um visual de DirectComposition, nao um elemento XAML. O upstream documenta: o
diagnostico de composicao do XAML "reconstroi um walker de arvore visual
process-wide sem lock toda vez que um visual DirectComposition e adicionado"
=> corrupcao de heap quando outra thread esta no mesmo codigo. O upstream impede
a CRIACAO do diagnostico de composicao respondendo a unica leitura que o
Windows.UI.Xaml.dll faz dentro do AdviseVisualTreeChange de
HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag (=1) — via hooks
inline em RegOpenKeyExW/RegQueryValueExW (vendor:19985-20020), que sao
proibidos aqui. E chama o Advise de uma THREAD NOVA porque da thread atual
"trava em Advising::RunOnUIThread as vezes" (vendor:11013-11017) — exatamente
os nossos HUNG. A forma do b919419 e a que o upstream tentou e abandonou
(linha comentada em vendor:11013).
Fato meu que corrige o spike: o snapshot do Plano 2 RECEBEU os mesmos 20 visuais
de composicao (estao no visual-tree.txt) e viveu, porque nunca os resolveu. A
morte deterministica e RESOLVER o handle de um visual de composicao; a corrida
de heap e o motivo probabilistico para nao deixar o diagnostico de composicao
existir sob uma assinatura permanente.

Ruling 4 — forma da Task 4, em tres partes, sem hook:
(a) nunca resolver handle cujo Type reportado comece com "Windows.UI.Composition."
    (enfileira a liberacao normalmente, como o snapshot ja fazia);
(b) chamar AdviseVisualTreeChange de uma thread nova (CreateThread no proprio
    processo, nao e injecao), como o upstream;
(c) DisableCompositionDiag: em vez de hook, o valor REAL no registro
    (HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag = 1), escrito uma
    vez com elevacao — e um switch de debug que o proprio XAML le por desenho,
    nao patch de codigo. O TAP le o valor antes de assinar; se nao estiver 1,
    NAO inicia a assinatura permanente e loga o motivo (falha fechado: o
    critério e nao corromper o heap do shell). O CLI ganha "setup" que escreve
    a chave via ShellExecute "runas" (Task 7).
Pendente de verificacao empirica antes de fechar: E8a (a+b sem registro) e E8b
(com a chave escrita pelo usuario). So entao a Task 4 e refeita.
Custo se errado: se a chave nao suprimir os visuais de composicao, sobra so (a),
que evita a morte deterministica mas nao a corrida; nesse caso a assinatura
permanente vira opt-in documentado como instavel e o Plano 3b busca outra via.

Decisao do usuario (AskUserQuestion): "Sim, mas o CLI faz isso por mim" — a
chave DisableCompositionDiag e escrita por um comando `taskbar-styler setup`
que pede elevacao (UAC). Para o experimento E8b, o spike escreve a chave via
Start-Process reg -Verb RunAs (mesmo prompt UAC que o setup vai mostrar), com o
usuario avisado para aceitar. Depois de E8a/E8b, a Task 4 e refeita com a
Ruling 4 mais: `setup` no CLI (ShellExecuteW "runas" em reg.exe, sem API de
injecao), `status` mostrando o estado da chave, `load` avisando se faltar.

Spike E8a/E8b (branch spike-standing-crash ate 4cc0c06): E8a (pular
Windows.UI.Composition.* + Advise em CreateThread) SOBREVIVEU — 1711 reports,
157 visuais de composicao pulados, 9 drenos, held volta a 0, Iniciar/central/
Task View OK. E8b (mesmo binario + DisableCompositionDiag=1, escrito com UAC
aceito pelo usuario) SOBREVIVEU com ZERO reports de composicao — o mecanismo do
upstream confirmado literalmente. Segundo `load` sem reiniciar: sobrevive mas o
TAP fica inerte (StartSubscription devolve S_FALSE sobre assinatura velha
enquanto OpenDiagnostics reabriu a sessao por baixo) — defeito separado.
Confound de metodo registrado pelo spike: o TryEnqueue do E5 ficou nos
experimentos seguintes; nao muda conclusao, mas o port nao deve leva-lo.
A chave de registro CONTINUA ESCRITA na maquina do usuario (consentida).

Ruling 4 confirmada e estendida: Task 4 refeita pelo implementador original
(retomado) com: filtro de Windows.UI.Composition.*; Advise em thread nova;
gate de registro FALHA FECHADO (sem a chave = sem assinatura permanente, so o
export do Plano 2); fix do registro de elementos (E4); StopSubscription no
SetSite com site nao nulo antes de reabrir; CLI `setup` (ShellExecuteExW
"runas" em reg.exe), `status` mostra a chave, `load` avisa; plano e spec
atualizados no mesmo commit.

Task 4 rework: DONE, commit cec2758 (sobre b919419). ctest 2/2. Smoke ao vivo:
mesmo PID do explorer do inicio ao fim, Responding: True, zero ERR, zero visuais
de composicao reportados (chave ligada), gate falha fechado (E_NOT_VALID_STATE)
testado contra nome de valor inexistente, segundo `load` sem reiniciar para e
reinicia a assinatura limpo.
Concerns do implementador: (1) `held` nunca chegou exatamente a 0 — mistura
duplicatas de handle de pai (o calculo pending - released ignora o dedupe) com
uma rajada numa thread de host transitoria (Task View) que nunca recebeu
segundo report para rearmar o dreno; upstream aceita o segundo caso ("a thread
que fica quieta segura a ultima rajada ate ser usada de novo"); (2)
SetSite(nullptr) continua sem caminho vivo. Review da task despachada (opus),
pacote d004f41..cec2758.

Task 4 review (opus): Spec OK, Quality NAO aprovada.
Important 1: Subscription com dois donos sem protocolo — a thread de Advise
deferencia s->callback depois do Advise e deleta em falha, enquanto
StopSubscription (segundo `load`, ~385 ms de janela) exchange+usa+deleta o mesmo
objeto; store(nullptr) incondicional pode apagar assinatura mais nova. Regressao
do rework (o b919419 tinha dono unico). Important 2: `held` = pending bruto
menos liberados, conta duplicatas de pai, nao estado; com ElementHasState=false
deveria ser 0 e logou 487/5 — o observavel da §7.2 esta errado.
Minors: comentario do header afirma que o lote chega na thread nova (falso: o
XAML leva a varredura para a thread de UI dona dos elementos — provado pelas
tags de thread no log; e por isso que o gate de thread inicializada nao
descarta o lote); invariante do E4 so meio aplicada; t_tick_token morto; sem
CoInitializeEx na thread (igual upstream, comentar); bloco do Step 4 do plano
ainda com o padrao antigo.
Task 4: minor (deferred): thread cuja ultima rajada nao e seguida de outro
report segura a rajada ate reportar de novo (desenho do upstream; documentado).
Fix round 1/5 despachado (implementador retomado) com a restricao: Stop NUNCA
espera a thread de Advise (Stop roda na UI thread; o Advise marshala para ela:
deadlock). Protocolo por estado atomico Advising/Advised/Stopped + CAS.

Task 4: fix round 1/5 (2 Importants + 5 minors endereçados, 0 abertos segundo o
implementador; commit cec2758..b0a5234). ctest core 96/96, tap 21/21. Segundo
`load` ao vivo: "drained 487 handles, 0 held". Ramo "CAS perdido, Stopped
primeiro" verificado por tracado manual das duas ordens, nao reproduzido ao vivo
(janela real mais estreita que 17 ms sem o trabalho da Task 5) — dito
claramente. Re-review escopada despachada (opus: protocolo de concorrencia).

Task 4 re-review round 1 (opus): 2-7 ADDRESSED (aritmetica do held e testes
conferidos; bloco do plano identico ao codigo). Finding 1 RESIDUAL: os dois
caminhos de falha (Advise falhou; CreateThread falhou) deletam Subscription
incondicionalmente depois de um CAS de g_subscription que, se FALHAR, significa
que um Stop ja pegou o ponteiro e vai deferencia-lo -> UAF/double-free.
Breakage novo (Important): o Stop diferido devolve com o callback antigo ainda
advised, e o SetSite segue para Close/Open/StartSubscription — a condicao E8b
(Advise novo com um velho registrado deixa o TAP sem reports ate reiniciar).
Ruling 5 — quando o Stop diferir, o chamador NAO prossegue: StopSubscription
devolve {None, Stopped, Deferred}; SetSite com Deferred loga "load ignored" e
devolve S_OK sem reabrir sessao nem reassinar (o callback antigo segue na
sessao antiga que ele mesmo segura por shared_ptr). Esperar seria deadlock na
UI thread. A mesma regra vale para o reload da Task 7 (Stop->Start): checar o
resultado e pular a reassinatura em Deferred. Custo se errado: um `load` ou
reload dentro da janela de ~400 ms do lote inicial e ignorado com log; o
usuario repete.
Task 4: fix round 2/5 despachado (1 residual + 1 novo).

Task 4 fix round 2: implementador derrubado de novo pelo limite de sessao,
logo no inicio (estava relendo os arquivos). Limite reiniciado; agente retomado
no mesmo contexto com instrucao de conferir git status antes de editar.

Task 4: fix round 2/5 (2 endereçados segundo o implementador; commit
b0a5234..6fd2e56). Caminhos de falha via helper TearDownAfterFailedAdvise com o
mesmo CAS de estado; StopResult{None,Stopped,Deferred}; SetSite ignora `load`
em Deferred. ctest core 96/96, tap 21/21. Ao vivo: dois `load` seguidos, mesmo
PID, par stopped/started nos dois, "drained 493 handles, 0 held". Nao exercitado
(so tracado a mao): ramos de falha concorrente com Stop. Re-review escopada
round 2 despachada (opus).

Task 4 re-review round 2 (opus): ambos ADDRESSED, tabela de 10 intercalacoes
com exatamente-uma-vez em Unadvise/refs/delete; sem Critical/Important novo.
Task 4: minor (deferred): no caminho de CreateThread falhando, o helper faz
Unadvise de um callback nunca advised e, como falha, vaza callback+sessao com
log enganoso (so sob falha de CreateThread; pre-fix liberava direto).
Task 4: minor (deferred): o ramo !site do SetSite ignora Deferred e fecha a
sessao global com um callback ainda vivo — sem caminho vivo hoje; os handles
que esse orfao enfileirasse vazariam (ReleaseHandle le a sessao global).
Task 4: nota para Task 7: a guarda Deferred e one-shot — um TERCEIRO SetSite
durante o mesmo Advise em voo ve None e reabre (forma E8b); fechar exige uma
flag "advise em voo" que sobreviva a Subscription.
Task 4: complete (commits d004f41..6fd2e56, review clean, 3 minors deferred)

Task 5 despachada (sonnet). BASE = 6fd2e56. Mantem review por task.

Task 5: DONE, commit 8027007. ctest 2/2 (+test_config). Smoke TranslucentTaskbar:
8 regras preparadas, 2 blur approximations; initial apply E=6 P=6 0 failed V=0
(reproduzivel). A TASKBAR ESTILIZOU: screenshot antes/depois mostra acrilico
translucido real (papel de parede atravessando), nao o fallback solido — sinal
para o Plano 3b (a aproximacao AcrylicBrush e visualmente boa na ilha).
Desvios do brief, todos por fatos medidos: (1) winrt_common.h precisou de
<winrt/Windows.UI.Xaml.Shapes.h> (erro de compilacao); (2) a linha "initial
apply" saiu do SetSite para o primeiro dreno em release_queue.cpp — no SetSite
ela le zero por construcao, porque StartSubscription retorna antes de o lote
marshalado comecar na mesma UI thread (medido: lote completa entre 5 e 59 s);
(3) std::hash<DependencyProperty> nao compila nesta STL/SDK (C2056 em
hash_base) — hash local sobre get_abi; (4) CoreDispatcher::TryRunAsync devolve
IAsyncOperation<bool>, nao IAsyncAction — tipo do t_delayed_fill corrigido.
Nao exercitado: restauracao ao vivo sem reiniciar (so na Task 7), estilos de
estado visual (V=0), hosts secundarios. Review da task despachada (opus).

Task 5 review (opus): Spec OK (os 4 desvios julgados corretos e minimos),
Quality NAO aprovada: C1 t_modifying vaza true quando SetOrClearValue lanca em
ApplyProperty (callbacks passam a retornar cedo para sempre); C2 o SetValue
adiado do BackgroundFill roda fora do guard e sobrescreve `original` com o
nosso proprio brush — o restore devolveria o acrilico, nunca o fill do shell
(invisivel no smoke porque so reiniciou o Explorer); I1 lambda adiada pode
lancar no dispatcher; I2 t_cache_theme ponteiro cru => ABA no reload de tema.
TODOS no codigo verbatim do meu plano. 8 minors, os baratos dobrados no round.
Task 5: minor (deferred): ramo de enum em PropertyEquals provavelmente morto
(IReference<TEnum> nao e IReference<int32_t>); fail-closed; Task 6 testa um
seletor [Prop=Enum] no smoke.
Task 5: minor (deferred): applied_properties conta o fill adiado antes de rodar.
Task 5: fix round 1/5 despachado (implementador retomado), com correcao do
texto do plano.

Task 5: fix round 1/5 (C1, C2, I1, I2 + 5 minors endereçados segundo o
implementador; commit 8027007..c3f4369). ModifyingGuard RAII em todos os
sites; lambda adiada inteira em try; t_cache_theme shared_ptr; plano corrigido.
Restore AO VIVO verificado sem reiniciar: gatilho temporario (removido antes do
commit) -> "restored 6 elements", taskbar voltou ao fundo solido padrao.
ctest 2/2. Re-review escopada despachada (opus).

Contexto da sessao em ~76%; usuario escolheu "continuar ate o fim do Plano 3".
Apos compactacao: retomar por este ledger + git log. Proximo passo quando a
re-review da Task 5 (agente a7ec434694ec725f7) voltar: se limpa, Task 5 complete
e despachar Task 6 (brief ja em task-6-brief.md; BASE = c3f4369); Task 6 mantem
review; Tasks 7 e 8 sem review por task, em lote; depois review final do branch
(MERGE_BASE = 8537aa7). Regras vivas: Deferred no reload da Task 7 (Ruling 5);
log de blur "N blur values approximated"; enum em PropertyEquals a testar no
smoke da Task 6.

Task 5 re-review round 1 (opus): 10/10 ADDRESSED, sem Critical/Important novo.
Task 5: minor (deferred): ~ModifyingGuard zera a flag em vez de restaurar o
valor anterior — sem aninhamento alcancavel hoje; a Task 6 (que adiciona
handlers) dobra o fix (bool prev_).
Task 5: minor (deferred): last_applied do fill adiado nunca e atualizado pela
lambda (direcao de erro segura: so pode gravar escrita EXTERNA como original).
Task 5: minor (deferred): RestoreAllOnThisThread sem chamador ate a Task 7;
ate la a thread segura o ResolvedTheme antigo (limitado a um por thread).
Higiene de relatorio: o "would fail on old code" das 2 novas config cases era
falso (o parser base ja rejeitava); ctest do fix round afirmado sem output.
Task 5: complete (commits 6fd2e56..c3f4369, review clean, 5 minors deferred)

Task 6 despachada (sonnet). BASE = c3f4369. Mantem review por task.

Task 6: DONE, commit 155e46a. ctest 2/2. Smoke Lucent: ciclo ActiveNormal ->
ActivePointerOver -> ActiveNormal no log, 0 failed, 0 deferred. Filtro de enum
em PropertyEquals estava MORTO (try_as<int32_t>); corrigido com GetInt32/
GetUInt32 do IPropertyValue e verificado ao vivo (HorizontalAlignment etc.).
Desvios do brief, por teste: VsgBucket::group como referencia FORTE (a weak_ref
nunca resolvia => CurrentStateChanged nunca disparava) — a review deve julgar o
risco de vida/vazamento; `claimed` por match e nao por linha de estilo
(vendor:15933-16031), senao o par @ActiveNormal/@ActivePointerOver de uma mesma
regra se anula. Ultima re-passagem de hover sem logs nao reproduziu a
transicao (input sintetico nao chegou nem ao flyout nativo) — checar com mouse
antes do merge. Review despachada (opus), pacote c3f4369..155e46a.

Task 6 review (opus): Spec OK; ambos os desvios ACEITOS (weak_ref nao podia
funcionar: peer DXaml sem ref forte nao resolve; claim por match e exatamente
vendor:16020-16031). Quality aprovada condicional a Q1 (Important):
CurrentStateName(group) avaliado fora de try em :565 — viola a restricao
"toda chamada WinRT em try" e deixa elemento meio-ligado se lancar. L2 estilo
@State sem grupo deve reivindicar a propriedade (fidelidade); L4/L6
comentarios; L5 plano nao sincronizado.
Task 6: minor (deferred): L3 propriedades so-incondicionais reescritas a cada
troca de estado (upstream pula quando nem o estado velho nem o novo tem
entrada) — so custo.
PORTAO DE MERGE (manual): um hover com mouse fisico num botao de app com um
tema de estados visuais, antes do merge — a ultima re-passagem sem logs nao
reproduziu (input sintetico nao chegou nem ao flyout nativo).
Task 6: fix round 1/5 despachado (Q1 + L2 + L4 + L5 + L6).

Task 6: fix round 1/5 (Q1, L2, L4, L5, L6 endereçados; commit 155e46a..56764c7).
Build limpo, ctest 2/2, Lucent: "initial apply: 178 elements, 356 properties,
0 failed, 0 deferred", zero ERR em ~964 linhas de log.
Ruling 6 — sem re-review escopada deste round: o revisor ja tinha aprovado
"condicional a Q1", Q1 e um try/catch de 3 linhas na forma padrao do arquivo,
os demais itens sao claim-antes-do-continue, comentarios e sincronizacao do
plano; a revisao final do branch le esse codigo. Custo se errado: um erro de
transcricao num try/catch, pego pela revisao final antes do merge.
Task 6: complete (commits c3f4369..56764c7, 1 minor deferred, portao manual de
hover pendente)

Tasks 7+8 despachadas em LOTE (sonnet), sem review por task (modo aprovado).
BASE = 56764c7. `setup` do CLI ja existe desde a Task 4 — nao duplicar.

Tasks 7+8: implementador derrubado pelo limite de sessao (2026-09-14, reset
1am). Limite reiniciado; agente retomado no mesmo contexto com instrucao de
conferir git status antes de editar. BASE continua 56764c7.

Tasks 7+8: DONE. Commits 5d09927 (Task 7) e 1b7c562 (Task 8). ctest core
105/105 (7643 asserts, +5 de variaveis de recurso), tap 21/21. AO VIVO, sem
reiniciar o Explorer: list -> apply TranslucentTaskbar -> status -> apply
Lucent -> reset -> apply Aeris, todos trocando a taskbar; apply Pills +
alternancia claro/escuro real: "merged 6 resource variables", sem crash, reset
identico ao padrao.
Achados reais do lote, para a revisao final triar:
(a) RestoreAllOnThisThread tira o snapshot da lista ANTES de restaurar, e
SetTheme(nullptr) so roda DEPOIS — elemento re-reportado como efeito colateral
da propria restauracao e estilizado contra o tema velho ainda ativo e escapa
(medido: 4 elementos sobreviveram a um reset ate o reload seguinte). Codigo da
Task 5/6, exercitado pela primeira vez pelo reload. Fix sugerido: limpar o tema
antes de restaurar (ou restaurar com o tema ja nulo).
(b) themes/Pills.json usa {{__unset}} como sentinela de "deixe sem valor"; o
skip de dinamicos do PrepareTheme nao reconhece, chega ao ResolveSetter e
falha (0x8000FFFF) 2x por apply — seguro por acidente, nao por desenho.
Nao exercitado: segundo monitor, Iniciar/central/flyout com tema aplicado, 10
min de estabilidade de handles (docs/smoke-test.md item 9) — para o usuario.
Task 7: complete (56764c7..5d09927, sem review por task)
Task 8: complete (5d09927..1b7c562, sem review por task)
TODAS AS 8 TASKS COMPLETAS. Revisao final do branch despachada (opus),
MERGE_BASE 8537aa7 .. 1b7c562.

REVISAO FINAL DO PLANO 3 (opus, 8537aa7..1b7c562): NAO ENVIAR — dois Importants,
ambos de uma linha, ambos os achados (a) e (b) do lote 7+8 confirmados:
B1 reload restaura com o tema velho ainda ativo (SetTheme(nullptr) so dentro de
LoadConfiguredTheme depois do restore); re-reports durante a restauracao sao
estilizados e ficam (4 Grids medidos) com handles retidos. Fix: SetTheme(nullptr)
antes de restaurar. B2 PrepareTheme testa {{ no texto CRU antes de aplicar
constantes; Pills poe {{__unset}} numa constante. Upstream (vendor:400-404):
{{Var}} indefinida => pula o estilo. Fix: checar {{ depois da substituicao +
teste. Pills e o unico tema com {{ em constante.
Nao-bloqueantes dobrados na onda: "reload applied" sempre zero
(t_initial_apply_logged nunca reseta); t_merged_theme ponteiro cru (mesmo ABA
do I2); CLI apply/reset imprimem sucesso sem checar Deferred/chave; nome de
teste obsoleto em test_corpus_resolution.cpp:81; doc smoke item 9 deve dizer
que o invariante e held == styled.
Ledger de minors triado: nenhum bloqueia; 3 ja fechados (enum, prev_, caller de
RestoreAll); os demais deferidos com razao verificada. Herdado pelo Plano 4: o
ramo !site ignorando Deferred e o UnregisterWaitEx(INVALID_HANDLE_VALUE) que
espera na UI thread um callback bloqueado em SendMessage — deadlock real quando
SetSite(nullptr) ganhar caminho vivo. Plano 3b: SendMessageTimeout no fan-out.
Verificado limpo pela revisao: disciplina de handles ponta a ponta, arbitragem
da assinatura em toda intercalacao, Deferred honrado nos dois chamadores,
restore (original/last_applied/guard/fill adiado/buckets/recursos), propriedade
de nao-injecao no codigo e no CI, fronteira e falha-fechado §7.6.
ONDA UNICA de correcao despachada (agente do lote 7+8 retomado); depois UMA
re-review escopada e finishing-a-development-branch.

Onda final: commit 6351313. B1 e B2 corrigidos e verificados ao vivo (reset
sem linhas "styled" entre "reload requested" e "restored 106", held=0; Pills
"49 rules prepared ... 51 dynamic values skipped", zero 0x8000FFFF). ctest core
106/106 (+1), tap 21/21. Cinco one-liners aplicados. Nao observado: dreno
pos-Pills (input sintetico nao gera mudanca de arvore) — evidencia estatica.
Re-review escopada UNICA despachada (sonnet, 1b7c562..6351313).

Re-review da onda final (sonnet): 7/7 ADDRESSED, sem breakage novo; o teste
novo de {{ em constante e guarda real (falharia na ordem antiga). Dois nits de
comentario pre-existentes ("initial apply" em release_queue.cpp:25-27 e
tap_boundary.cpp:200) — deferidos.
PLANO 3: COMPLETO no codigo (8537aa7..6351313, 19 commits). Pendente: portao
manual de hover (usuario) e integracao (finishing-a-development-branch).
Ledger e spike preservados em docs/superpowers/plano-3-*.md; workspace removido.
