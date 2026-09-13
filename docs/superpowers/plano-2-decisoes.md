# SDD ledger — plan: docs/superpowers/plans/2026-09-12-plano-2-tap-e-arvore-visual.md

Branch: `plano-2-tap-arvore-visual`
Base: `d704c26`
Spec: `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md`
Decisoes do Plano 1: `docs/superpowers/plano-1-decisoes.md`

## Pre-flight scan

### Pares que compartilham arquivo ou interface

| A | B | Produz -> Consome | Achado |
|---|---|---|---|
| T1 | T2,T4,T5,T6 | `src/tap/CMakeLists.txt` criado -> cada uma soma fontes | CONFLITO, ver Ruling 1 |
| T1 | T3,T6 | `src/cli/main.cpp` esqueleto -> subcomandos | OK, T3 substitui o arquivo inteiro |
| T1 | T3,T5 | `tests/tap/CMakeLists.txt` -> soma testes | OK |
| T2 | T4 | `g_site` (IUnknown*) -> `OpenDiagnostics(site)` | OK |
| T4 | T5 | `AcquireSession()` -> `ExportTreeToFile` | OK |
| T5 | T1 | `tree_format.cpp` precisa estar nos DOIS alvos | CONFLITO, ver Ruling 1 |
| T6 | T2 | `InitThunkPublic` usado no SetSite | CONFLITO, ver Ruling 2 |
| T3 | T1 | teste compila `src/cli/loader.cpp` e linka a lib | OK, `clsid.h` e header-only |

### Auto-consistencia de cada tarefa

| Tarefa | Achado |
|---|---|
| T1 | OK — dois testes de log, ambos verificaveis |
| T2 | OK — sem teste unitario por ser COM; verificacao e o smoke da T3, declarado |
| T3 | OK — so `DescribeHresult` e testavel, e e o que os testes cobrem |
| T4 | OK — GUID real ja no texto, com passo de conferencia |
| T5 | OK — parte pura separada em `tree_format.cpp` justamente para ser testavel |
| T6 | OK |
| T7 | OK — o grep anti-injecao no CI transforma o argumento do projeto em teste |

### Rulings

**Ruling 1 — a DLL linka a lib estatica em vez de recompilar as fontes.**
O plano lista `log.cpp` nas fontes dos dois alvos e a T5 manda somar
`tree_format.cpp` "a ambos". Isso cria uma classe de bug de "somei num, esqueci
no outro" que so aparece no link.
Decisao: `taskbar_styler_tap_lib` recebe todas as fontes puras (log, tree_format,
e o que mais surgir); `taskbar_styler_tap` recebe apenas as fontes com dependencia
de COM/XAML (tap_boundary, visual_tree_watcher, tree_export, thread_init) e
`target_link_libraries(taskbar_styler_tap PRIVATE taskbar_styler_tap_lib)`.
Cada fonte fica em exatamente um lugar.
Custo se errado: se um simbolo puro precisar ser exportado pela DLL, o linker
pode descarta-lo; corrige-se com /WHOLEARCHIVE ou movendo a fonte.

**Ruling 2 — `InitThunkPublic` e definido em `thread_init.h`, nao no boundary.**
A T6 manda o `tap_boundary.cpp` definir um thunk no namespace anonimo e usa-lo
acima da definicao. Alem da ordem, isso poe logica no arquivo cuja regra e conter
so a fronteira.
Decisao: `thread_init.h` exporta `void WINAPI InitializeForCurrentThreadThunk(void*)`,
implementado em `thread_init.cpp`; o boundary so chama.
Custo se errado: um nome a mais na superficie de `thread_init`.

## Progresso

Task 1: implementado (commit 58606d8). 2 TEST_CASE de log; DLL e CLI compilando.
Ruling 1 aplicada: log.cpp so na lib estatica, DLL com placeholder dllmain.cpp
e linkando a lib.
Task 1: revisao — spec OK, qualidade aprovada. 1 Important, 3 minors.
Task 1: desvio do implementador adjudicado pelo revisor e por mim: tap.def criado
mas nao ligado por /DEF:, porque DllGetClassObject/DllCanUnloadNow so existem na
Task 2. Correto — ligar agora daria LNK2001. O revisor notou que o proprio
template do brief teria falhado igual se digitado como estava.

Task 1: Ruling 3 — nao abrir rodada de fix pelo Important (o lembrete do /DEF:
mora so no relatorio, nao no CMakeLists). A Task 2 edita exatamente esse arquivo
para ligar o /DEF:, e eu carrego a exigencia no despacho dela. O risco do
achado — "a Task 2 esquece" — e eliminado na origem, sem gastar uma rodada num
comentario de uma linha.
Custo se errado: se a Task 2 falhar por outro motivo e for redespachada sem o
meu texto, o lembrete se perde. Mitigado por esta linha do ledger.

Task 1: minor (deferido): _wfopen_s/fclose por linha de log; so importa se o
nivel Debug virar gargalo medido.
Task 1: minor (deferido): clsid.h nao e incluido por ninguem ainda, entao sua
compilacao nao e verificada por este ctest. A Task 2 resolve.
Task 1: minor (deferido): strings de usuario do CLI em portugues, vindas do
proprio brief; a regra de idioma cobre identificadores e comentarios.
Task 1: complete (commits d704c26..58606d8, review clean)

Task 2: implementado (commit c816abf). dumpbin confirma exports exatamente
DllGetClassObject e DllCanUnloadNow. .def ligado, dllmain.cpp removido.
Task 2: revisao — spec OK, qualidade NAO aprovada. 2 Important, 2 minors.
O revisor tracou a contagem de referencias das duas classes COM e o ciclo
SetSite(x)/SetSite(null)/SetSite(y): balanceado, sem vazamento.

Task 2: Ruling 4 — GetSite CONTINUA devolvendo E_FAIL; quem muda e a spec.
O achado esta factualmente certo (a §6.4 nomeia GetSite na lista do sempre-S_OK)
e a conclusao esta errada: E_FAIL sem site e o contrato correto de
IObjectWithSite. S_OK significa "*ppv e valido"; devolve-lo com ponteiro nulo
mentiria para todo chamador, e o upstream tambem nao forca S_OK ali.
O proposito da regra e nao silenciar o XAML — um erro devolvido de um callback
faz o XAML parar de reportar. Isso vale para OnVisualTreeChange,
OnElementStateChanged e SetSite, nao para funcoes cujo HRESULT e uma resposta
de verdade. Meu texto de spec generalizou demais; §7.1 sera escopada.
Custo se errado: se algum caminho do XAML realmente exigir S_OK de GetSite, a
ativacao falha — mas nada na sequencia observada chama GetSite de volta.

Task 2: Ruling 5 — g_site vira std::atomic<IUnknown*> com leitor em site.h,
antes de a Task 4 e a Task 5 consumirem o global cru de outras threads.
Hoje a sequencia pode ser mono-thread; deixar um global sem sincronizacao para
tarefas futuras consumirem e uma corrida esperando ser escrita.
Custo se errado: uma indirecao a mais numa leitura que acontece poucas vezes.

Task 2: minor (deferido): <inspectable.h> incluido sem uso, vindo do brief;
a Task 4 passa a usar.
Task 2: fix round 1/5 (3 addressed, 0 open — spec §7.1 escopada separando
"nunca lanca" de "nunca devolve erro"; g_site atomico com leitor em site.h,
publicado so depois do AddRef e limpo antes do Release; omissao do FreeLibrary
documentada; commit b470230). §6.4 nao continha a lista, o texto errado estava
so na §7.1.
Task 2: complete (commits 58606d8..b470230, review clean)

Task 3: implementado (commit 9c1245a). SMOKE TEST PASSOU: TAP residente no
explorer.exe pid 5520, confirmado por Get-Process Modules, com SetSite e
"loaded into C:\WINDOWS\Explorer.EXE" escritos de dentro do processo.
Task 3: revisao (opus) — spec OK, qualidade NAO aprovada. 1 Critical, 5 minors.
O revisor confirmou que o laco de slots VisualDiagConnection nao e retry loop:
avanca so em ERROR_NOT_FOUND e para em qualquer outro HRESULT.

Task 3: Critical — GetLogLevel() sobrescrevia SetLogLevel(), e o ctest passou a
depender de um arquivo fora do repo (%LOCALAPPDATA%\...\loglevel.txt).

Task 3: Ruling 6 — remover o loglevel.txt inteiro; o padrao de log vira Info.
O implementador resolveu um problema real, criado por eu ter enquadrado o padrao
como "questao de design" sem dizer para que lado. Mas o mecanismo e maior que o
problema: SetSite e "loaded into" disparam uma vez por carga, nao por elemento.
O caminho quente e o OnVisualTreeChange, que loga em Debug e continua fechado.
Entao nao ha nada a proteger com o padrao Error.
Isso apaga ~55 linhas sem teste, devolve a hermeticidade da suite e faz o smoke
test enxergar a propria evidencia sem superficie nova. Spec §7.3 emendada.
Custo se errado: se algum dia um evento de Info virar frequente, o padrao passa a
custar; a mesa da §7.3 ja diz que o custo de Info e desprezivel.

Task 3: minor (deferido): Usage() anuncia status e unload, que so existem na
Task 6; hoje o CLI responde "comando desconhecido" e em seguida os lista.
Task 3: minor (deferido): sem CoUninitialize (inofensivo na saida do processo).
Task 3: minor (deferido): sem revalidar que o pid ainda e do explorer entre
FindTaskbarPid e LoadTap; corrida de reuso de PID, no pior caso um HRESULT.
Task 3: minor (deferido): kMaxConnectionAttempts = 10000 vindo do brief; se um
alvo de outra sessao tambem devolver ERROR_NOT_FOUND, sao 10 mil tentativas
antes de reportar. Baixar para ~64 numa tarefa futura.
Task 3: layout de artefatos mudou — a DLL agora sai em build/src/cli/, ao lado do
exe, para o TapDllPath encontra-la. O check de CI da Task 7 no plano apontava
para build/src/tap/ e esta sendo corrigido nesta rodada.
Task 3: fix round 1/5 (1 Critical addressed, 0 open — loglevel.txt removido por
inteiro, padrao vira Info, GetLogLevel volta a ser load atomico, spec §7.3
emendada, caminho de CI do plano corrigido; commit e7a491e). O re-revisor
raciocinou que o teste novo REALMENTE falha contra o bug restaurado, nao e
tautologia.
Task 3: complete (commits b470230..e7a491e, review clean)

Task 3: Ruling 7 — o usuario autorizou reiniciar o Explorador durante o
desenvolvimento. A DLL nunca se descarrega (DllCanUnloadNow devolve S_FALSE por
design), entao ela trava o relink com LNK1168 depois de cada smoke test, e as
Tasks 4-6 modificam a DLL. Implementadores podem reiniciar o explorer.exe entre
build e smoke test quantas vezes precisarem.
Custo se errado: as janelas do Explorador de Arquivos do usuario fecham a cada
reinicio; nenhum outro programa e afetado.

Task 4: implementado (commit 28f4731), smoke test limpo, GUID conferido.
Task 4: revisao (opus) — spec OK, qualidade NAO aprovada. 3 Critical, 4 Important.
TODOS os achados vem do codigo que eu prescrevi no plano, nao de escolha do
implementador.
  Critical 1: LiveHandleCount deriva monotonicamente negativo — incrementa por
  Add e decrementa por handle liberado, entao um elemento adicionado e removido
  soma -2. O contador que a §7.2 elege como observavel do vazamento nunca
  poderia mostrar vazamento.
  Critical 2: liberar handle de dentro do OnVisualTreeChange. O upstream diz que
  o report chega "de dentro da varredura Leave que ainda esta visitando a
  subarvore", que liberar ali "destroi o elemento no meio da varredura", e chama
  isso de "a unica coisa que nao e segura" (vendor:11168, :18379, :18404). Ele
  resolve enfileirando e drenando no dispatcher; nada no Plano 2 agenda essa
  drenagem.
  Critical 3: g_diagnostics e g_hooks sao globais sem sincronizacao que uma
  thread de callback le enquanto outra libera — use-after-free.

Task 4: Ruling 8 — o Plano 2 abandona o AdviseVisualTreeChange inteiro.
Nada no Plano 2 consome o fluxo por elemento: a Task 5 exporta a arvore
caminhando sob demanda por GetVisualRoots/GetChildren. Notificacao de mudanca e
o que o Plano 3 precisa, para estilizar elemento que aparece depois. Entao a
assinatura nao compra nada aqui e custa risco de crash dentro do shell do
usuario. Removendo-a, os Criticals 1 e 2 somem junto.
Fica a sessao de diagnostico (IXamlDiagnostics + TestHooks + Diagnostics()), com
tempo de vida corrigido, e o ReleaseHandle, porque a caminhada sob demanda da
Task 5 tambem recebe handles que precisam ser liberados.
A assinatura e o desenho da drenagem diferida vao para o Plano 3, com as tres
referencias de linha do upstream registradas no plano.
Custo se errado: o Plano 2 nao prova o caminho de callback; o Plano 3 descobre
qualquer surpresa dele mais tarde, quando ha mais codigo novo em volta.

Task 4 fix round 1: commit b86aa0b. Removeu AdviseVisualTreeChange e o
IVisualTreeServiceCallback2; renomeou StartWatching/StopWatching para
OpenDiagnostics/CloseDiagnostics; g_diagnostics/g_hooks viraram um
DiagnosticsSession sob um unico ponteiro atomico com portao unico de teardown;
contador so incrementa em release bem-sucedido; guarda handle==0; loga falha de
UnregisterInstance; double-open fecha e reabre. ctest 2/2. Smoke: `load` loga
"diagnostics session open" no lugar de "watching the visual tree", sem
"IXamlDiagnosticsTestHooks unavailable". Re-review escopada despachada (opus).
Concern do implementador: a Task 6 do plano ainda cita StartWatching/StopWatching
em dois pontos — fora do escopo autorizado da Ruling 8, precisa ser renomeado
antes de a Task 6 ser despachada.

Task 4 re-review round 1 (opus): itens 2, 3, 5, 6 ADDRESSED; item 1 ADDRESSED
(as tres referencias obsoletas do plano estavam so no working tree; commitadas
por mim em dbffdde). Item 4 NOT ADDRESSED — o refactor moveu g_diagnostics/
g_hooks para DiagnosticsSession sob atomic<DiagnosticsSession*>, mas trocou o
ponteiro velho por use-after-free de heap: CloseDiagnostics faz exchange(nullptr)
e delete sem esperar leitor em voo.

Task 4: Ruling 9 — atomic<shared_ptr<DiagnosticsSession>>, nao SRWLOCK.
O reviewer propos SRWLOCK + contrato "AddRef uma copia privada" no ponteiro
retornado. Rejeitado: ponteiro cru atravessando a fronteira de retorno nao pode
ser protegido por lock com escopo da chamada — e por isso mesmo que o contrato
ja documentado no visual_tree_watcher.h:41-42 e inimplementavel (o chamador so
consegue AddRef depois de ja segurar um ponteiro possivelmente liberado). Regra
que mora em comentario e regra que o implementador da Task 5 viola escrevendo o
codigo obvio. shared_ptr poe o tempo de vida no tipo: Diagnostics() morre,
AcquireSession() devolve a referencia forte, CloseDiagnostics vira store(nullptr),
OpenDiagnostics vira exchange — o que mata de graca o terceiro achado (dois opens
concorrentes vazando a sessao sobrescrita).
Custo se errado: atomic<shared_ptr> nao e lock-free na MSVC, entao ReleaseHandle
paga um spinlock por elemento na travessia. Centenas de elementos; irrelevante.

Task 4: Ruling 10 — LiveHandleCount vira ReleasedHandleCount e a spec §7.2 cede.
O header afirmava satisfazer o observavel de vazamento da §7.2, que pede contador
de handles VIVOS e diz que crescimento monotonico indica bug. Um contador de
handles liberados cresce monotonicamente quando esta saudavel e nao consegue
mostrar handle nao liberado. As duas coisas nao podem ser verdade. A §7.2 cede
porque a premissa dela e do Plano 3: a travessia do Plano 2 libera tudo dentro da
propria travessia, entao nao ha nada vivo para contar.
Custo se errado: o Plano 3 precisa reintroduzir o medidor vivo; ele ja vai mexer
nessa contabilidade de qualquer forma para segurar elementos ao longo do tempo.

Fix round 2 despachado (implementador retomado).

Task 4 fix round 2: commit 2e3862f. ctest 2/2; smoke `load` OK. Re-review
escopada round 2 despachada (opus).
Concern honesto do implementador, registrado porque muda o que o Plano 3 pode
supor: CloseDiagnostics/SetSite(nullptr) nao e exercitado por caminho vivo
nenhum. O CLI nao tem `unload`, o `unload` planejado da Task 6 nao chama
SetSite(nullptr) de proposito (a DLL fica residente ate o explorer reiniciar), e
taskkill e morte dura sem teardown. Zero linhas de SetSite(nullptr) nas rodadas
0-2 apesar de muitos restarts. Entao a correcao de tempo de vida esta verificada
por leitura de codigo, nao por corrida executada — o unico caminho que realmente
destroi uma sessao hoje e o double-open via exchange.

Task 4 re-review round 2 (opus): os tres achados ADDRESSED, e o revisor
percorreu o argumento de interleaving linha a linha — atomic<shared_ptr>::load
faz o incremento do refcount atomicamente em relacao a exchange/store no mesmo
atomic, entao nao existe janela entre ler o ponteiro e segurar a referencia. O
double-open tambem fecha: cada sessao e o `previous` de exatamente um chamador.
Mas o fix introduziu um Important, e e contra a minha Ruling 9:
atomic<shared_ptr<>> NAO e trivialmente destrutivel, entao virou destrutor de
escopo de namespace. Como CloseDiagnostics nunca roda em caminho vivo, g_session
esta normalmente cheio na saida — numa saida graciosa do explorer (logoff,
shutdown, "Sair do Explorer") o CRT roda a atexit table no DLL_PROCESS_DETACH e
chama Release() em objetos COM do XAML sob o loader lock durante o teardown do
shell. Forma classica de hang/AV no shutdown. Nao apareceu em tres rodadas de
smoke porque todo encerramento observado foi taskkill, e TerminateProcess nao
roda DllMain.
Ruling 11: vazamento intencional. g_session vira ponteiro heap nunca destruido,
para nao registrar destrutor nenhum. Isso e coerente com a doutrina que o modulo
ja tem escrita (DllCanUnloadNow devolve S_FALSE, tap_boundary.cpp:75-79): este
TAP nao desmonta, por projeto. Vazar uma sessao na saida do processo e a metade
barata e segura do trade.
Custo se errado: uma sessao vazada por processo explorer, que ja ia morrer.
Mais dois minors mandados junto: S_FALSE acumulando dois papeis (sentinela de
"sem hooks" e retorno real do UnregisterInstance, fazendo o contador
sub-reportar — regressao que o rename atravessou), e catch(bad_alloc) estreito
demais num modulo cuja regra e falhar fechado.
Fix round 3 despachado.
Pendente meu: o diagrama do §4.1 da spec ainda desenha AdviseVisualTreeChange
como caminho vivo, e a tabela de interfaces T2->T4/T4->T5 deste ledger (linhas
17-18) ainda cita StartWatching/Diagnostics(). Corrijo depois do round 3 para
nao disputar o working tree com o implementador.

Task 4 re-review round 3 (sonnet): todos os tres achados ADDRESSED, sem breakage
novo. As cinco acessos a g_session conferidos como conjunto (decl, ReleaseHandle,
AcquireSession, OpenDiagnostics, CloseDiagnostics); nenhum outro objeto de escopo
de namespace com destrutor nao-trivial no arquivo; o argumento de tempo de vida
segue valendo palavra por palavra.
Task 4: complete. HEAD do fix: 94aadf8.
Docs meus fechados: 2d9a85d (diagrama §4.1 separa Plano 2 / Plano 3) e a tabela
de interfaces deste ledger.
Carrego para a review da Task 5: `AcquireSession()->diagnostics()` numa expressao
so devolve ponteiro cru cujo dono morre no fim da full-expression. O header e o
brief avisam; a review tem que conferir a FORMA da chamada nos call sites, nao so
que a chamada existe.
Carrego para o final review: log.h:13 e log.cpp:16 justificam o gate de log
citando OnVisualTreeChange, que nao existe mais.

Task 5 despachada (sonnet). BASE = 2d9a85d.

Task 5: BLOCKED. Defeito de plano, meu, e grave.
O implementador foi ler o xamlOM.h do SDK 10.0.26100.0 antes de escrever codigo e
achou que IVisualTreeService/2/3 NAO tem GetVisualRoots, GetChildren nem
GetVisualElement. Conferi eu mesmo: o vtable completo e AdviseVisualTreeChange,
UnadviseVisualTreeChange, GetEnums, CreateInstance, GetPropertyValuesChain,
SetProperty, ClearProperty, GetCollectionCount, GetCollectionElements, AddChild,
RemoveChild, ClearChildren, GetPropertyIndex, GetProperty, ReplaceResource,
RenderTargetBitmap, ResolveResource, GetDictionaryItem, AddDictionaryItem,
RemoveDictionaryItem. Eu inventei os tres nomes.

Isso derruba a justificativa central da Ruling 8, que dizia que a assinatura de
notificacao nao comprava nada porque "a Task 5 percorre a arvore sob demanda via
GetVisualRoots/GetChildren". A premissa era falsa. A conclusao pode ou nao
sobreviver — e o que o spike vai dizer.
Esta e a segunda vez neste projeto que eu afirmo um fato tecnico como evidencia
de uma ruling sem conferir (a primeira foi a Ruling 10 do Plano 1, sobre o
round-trip). O padrao e o mesmo: a evidencia soa plausivel, ninguem a checa
porque veio do controlador, e ela vira arquitetura.
O implementador acertou em recusar reverter a Ruling 8 sozinho e escalar.

Contaminado pela premissa falsa, para arrumar depois da decisao: secao Task 4 do
plano, brief da Task 5, nota §7.2 e diagrama §4.1 da spec (commit 2d9a85d),
comentarios do visual_tree_watcher.h, e as Rulings 8-11 deste ledger.

Fato novo que muda o quadro: o upstream tambem nunca teve esses metodos. Ele
percorre filhos com Media::VisualTreeHelper::GetChildrenCount/GetChild
(vendor:18099-18101) — WinRT puro, fora da API de diagnostico — e usa a
assinatura so para ser NOTIFICADO. E ele deriva handle de elemento alcancado
"por outro caminho, ex. caminhando a arvore visual" (vendor:10954-10965). Ou
seja: caminhada sob demanda existe; so nao pela API que eu citei.
Sobra tambem uma via COM pura nao explorada: GetPropertyValuesChain para achar a
propriedade Children de um elemento, e GetCollectionCount/GetCollectionElements
para enumera-la — os dois estao no vtable acima.

Spike despachado (opus, branch spike-pull-walk) para responder com codigo
rodando: (Q1) da para obter um elemento vivo da taskbar sem assinar, via
GetUiLayer/GetApplication; (Q2) da para enumerar filhos so com COM, via
GetPropertyValuesChain + GetCollectionElements; (Q3) da para chegar na raiz da
taskbar a partir do que Q1 devolver.
Nao vou rular de novo em cima de teoria. Foi teoria que produziu este bloqueio.

Spike concluido (branch spike-pull-walk, commit 1a93f13).
Q1 NAO: nao existe porta sem assinatura ate a arvore da taskbar. GetUiLayer
devolve S_OK mas entrega um Grid DESTACADO, Parent=null e zero filhos (a camada
de adorno do diagnostico). GetApplication devolve XamlExplorerHost.XamlApplication,
que o tree service nao conhece (GetPropertyValuesChain -> 0x80070490). HitTest ->
E_INVALIDARG em todo espaco de coordenada tentado. Window.Current.Content nulo
(host de island).
Q2 SIM, com dois poréns: Children aparece com bits=0x5, GetCollectionCount
funciona e GetCollectionElements devolve handles reais. Porem (a) pElementCount e
in/out apesar de o header declarar [out] — passar 0 devolve S_OK com zero
elementos, entao seguir o header ao pe da letra faz concluir, errado, que colecao
nao enumera; e (b) controle com template nao expoe filho visual —
Taskbar.TaskbarFrame tem 265 propriedades e nenhuma e Children/Content/Child,
entao a travessia por propriedade morre no terceiro nivel.
Q3 NAO a partir do Q1.

Ruling 12 — a Task 5 tira um INSTANTANEO: Advise, lote inicial, Unadvise, e so
entao formata e libera.
O fato que decide vem do spike e eu nao teria adivinhado: o lote inicial chega
SINCRONO dentro da propria chamada de AdviseVisualTreeChange, na thread do
SetSite. reported=200 ja era verdade quando a chamada retornou, no mesmo
milissegundo. Nao nasce thread de callback nenhuma para o instantaneo, e liberar
os 200 handles depois do Unadvise, fora de qualquer callback, funcionou limpo.
Entao o perigo que motivou a Ruling 8 — liberar de dentro do Leave walk — nao
aparece aqui, e o dreno adiado continua sendo problema do Plano 3, que e quem
mantem assinatura de pe.
A arvore sai do stream, nao de travessia por propriedade: o stream reporta
ParentChildRelation{Parent, Child, ChildIndex} para todo elemento, inclusive os
que tem template e nao expoem Children. Ou seja, o Q2 nem e usado — a via COM
pura que eu esperava salvar o desenho existe, mas e a via errada.
O que sobra da Ruling 8: o Plano 2 nao mantem assinatura de pe. O que cai: a
justificativa que eu dei para isso.
Custo se errado: se o lote deixar de chegar sincrono numa versao futura do
Windows, o export sai vazio — e por isso ele falha alto (E_FAIL + log) em vez de
escrever arquivo vazio e dizer sucesso.

Step 6 da Task 5 reescrito no plano com o desenho do instantaneo. Texto
contaminado pela premissa falsa corrigido em: secao Task 4 do plano (justificativa
+ comentario do header + doc do ReleaseHandle), bloco Consumes da Task 5,
diagrama §4.1 e nota §7.2 da spec, e comentarios do src/tap/visual_tree_watcher.h.

Decisao do usuario (perguntou por que estava demorando; ofereci duas alavancas e
ele aprovou as duas): Tasks 6 e 7 vao num despacho unico e SEM review por task —
so a review final do branch inteiro. A Task 5 mantem review propria, porque mexe
com liberacao de handle e com callback rodando dentro da varredura do XAML; 6 e 7
sao ciclo de vida de janela, documentacao e CI.

Task 5: DONE, commit cbf408c. ctest 2/2 (10 TEST_CASE, 22 asserts, 4 novos em
test_tree_format.cpp). Smoke ao vivo: 523 elementos exportados, e o alvo real do
themes/TranslucentTaskbar.json (Taskbar.TaskbarFrame > Grid#RootGrid >
Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill) aparece aninhado
nessa ordem exata no visual-tree.txt. A entrega do plano funciona.
Concerns do implementador, todos honestos: LogLevel::Warn nao existe (usou Error);
nomes de tipo saem completos (Windows.UI.Xaml.Controls.Grid) e nao curtos como o
JSON dos temas usa, com evidencia do AdjustTypeName do upstream (vendor:18795) de
que a expansao e do lado do tema — o matcher do Plano 3 vai precisar replicar isso;
BuildForest/Materialise sem teste unitario; cap de profundidade nunca exercitado.

Ruling 13 — BuildForest/Materialise ganham teste. Sao logica pura de verdade
(dedupe primeiro-reporte-vence, orfao-vira-raiz, ordenacao estavel por ChildIndex,
cap de profundidade) e hoje moram no namespace anonimo do alvo DLL, onde teste
nenhum linka. Vao para o lib estatico com testes, na rodada de correcao. Declarei
isso adiantado para o revisor nao gastar seat re-derivando.
Custo se errado: mais um arquivo no lib estatico; nenhum.

Review da Task 5 despachada (opus) EM PARALELO com a implementacao do lote 6+7
(sonnet, BASE cbf408c) — a review e read-only sobre um intervalo fixo, entao nao
disputa nada com o implementador.

Review da Task 5 (opus): Spec NAO CUMPRIDA, Quality NAO APROVADA. 2 Criticals,
3 Importants, 5 Minors. O achado principal e forte e tem precedente no upstream:

Critical 1 — relation.Parent nunca e liberado. O loop de liberacao
(tree_export.cpp:201-203) solta so r.handle. O upstream solta OS DOIS em todo Add
(vendor:11162-11165: QueueDiagnosticsRelease(element.Handle) E
QueueDiagnosticsRelease(relation.Parent)), porque cada report e uma entrega
independente — UnregisterInstance "fecha o objeto runtime cacheado para um
handle, a unica referencia que o diagnostico guarda de um elemento depois de
reporta-lo" (vendor:10942-10944). Na corrida observada isso vaza ~522 registros
fixando objetos XAML pela vida do shell. E exatamente o vazamento da §7.2 que o
brief manda evitar. Reported::parent ja esta guardado; e uma linha.
Mesma causa, segunda instancia: um Remove que chegue no meio do lote e descartado
sem liberar element.Handle (upstream libera, vendor:11169-11171).

Critical 2 — sem RAII no loop de liberacao, tres caminhos retornam com handle
pendente: Advise que reporta e depois devolve HRESULT de falha; e bad_alloc em
BuildForest/AssignSiblingIndices/append de wstring, que desenrola por cima do
loop ate o catch(...) do SetSite. Um scope guard logo depois do Advise fecha os
tres e torna a ordem ("depois do Unadvise, fora de callback") estrutural em vez
de posicional.

Important 3 — HRESULT do Unadvise descartado com callback na PILHA. Se falhar, o
objeto morre no fim da funcao e o XAML segue com o ponteiro; a proxima mutacao
faz chamada virtual num quadro de pilha reusado. Crash sem atribuicao dentro do
explorer, sem nada no log.
Important 4 — a guarda "nao escreva arquivo vazio e chame de sucesso" olha o
valor errado: checa reported.empty() mas o que se escreve e `out`. Ciclo nas
relacoes reportadas => roots vazio => arquivo de zero byte, log "tree exported"
em Info, S_OK.
Important 5 — o cap de profundidade nao guarda o que o comentario dele afirma.
Cada indice entra em no maximo um kids[], entao o grafo alcancavel e funcional e
nenhum membro de ciclo e alcancavel a partir de uma raiz; profundidade ja e
limitada por reported.size(). O sintoma real de ciclo e o achado 4.

O revisor tambem confirmou os tres julgamentos do implementador (Warn inexistente,
nome de tipo completo com a citacao do AdjustTypeName conferida em
vendor:18795-18801, cap nao exercitado) e confirmou que mover BuildForest para o
lib estatico nao quebra nada, desde que a struct de entrada declare o handle como
unsigned long long para o lib seguir sem xamlom.h.

Fix round da Task 5 SEGURADO ate o lote 6+7 reportar: as correcoes tocam
tap_boundary.cpp e src/tap/CMakeLists.txt, que o lote esta editando agora. Dois
agentes escrevendo os mesmos arquivos na mesma arvore de trabalho e corrupcao.

Tasks 6 e 7: DONE_WITH_CONCERNS. Commits 744b024 (Task 6) e d592031 (Task 7).
Build limpo em /W4, ctest 2/2, CLI load/status/unload exercitados ao vivo
("initialized for thread", "host watch started", cadeia da arvore visual).
Desvio: renomeou host -> xaml_host num laco do tap_boundary.cpp para matar
shadowing C4456 contra o wchar_t host[MAX_PATH] declarado acima no SetSite. Sem
mudanca de comportamento nem de interface.
Nao exercitado, e o motivo e do ambiente, nao do codigo: deteccao de host novo
("new XAML host"). O sandbox nao tem desktop interativo, entao Win+A simulado nao
abre o flyout de configuracoes rapidas — confirmado por enumeracao de janelas,
nenhum XamlExplorerHostIslandWindow jamais apareceu. Idem segundo monitor e a
vigilia de 10 minutos de estabilidade de memoria (so instantaneo pontual).
Esses tres itens ficam para o usuario rodar na maquina dele; a checklist do
docs/smoke-test.md ja os cobre item a item.

Fix round 1 da Task 5 despachado sobre d592031, com os 2 Criticals, os 3
Importants, os 4 Minors acionaveis e a Ruling 13 (mover BuildForest/Materialise
para o lib estatico com testes) num lote so.

Task 5 fix round 1: commit 5c99b17. Todos os 2 Criticals, 3 Importants e 5 Minors
endereçados, mais a Ruling 13. ctest 2/2 (17 TEST_CASE, 37 asserts — 7 novos
cobrindo BuildForest e as lacunas de formatacao). Smoke re-rodado: mesma cadeia
aninhada (Taskbar.TaskbarFrame#TaskbarFrame > Grid#RootGrid >
Taskbar.TaskbarBackground#BackgroundControl > Grid > Rectangle#BackgroundFill[1]),
341 elementos nesta corrida contra 523 na anterior — variacao real de estado da
taskbar entre corridas, nada no codigo depende de contagem fixa.

Desvio do implementador na leitura literal do Critical 2, e ele esta certo:
manteve o UnadviseVisualTreeChange sincrono e imediato em vez de dobra-lo no
destrutor do guard RAII. Se o Unadvise so acontecesse na saida da funcao, a
assinatura ficaria viva — e o buffer do callback alcancavel por mutacao
concorrente em outra thread de UI do explorer — durante toda a janela de
BuildForest/formatar/escrever, que e exatamente a corrida que o desenho sincrono
da Ruling 12 existe para evitar. O guard ainda dispara estritamente depois do
Unadvise ter retornado, entao a garantia de ordem continua valendo; so nao e o
guard que faz a chamada. Sinalizado para o revisor final conferir.

Re-review escopada do fix round DISPENSADA de proposito: a revisao final do branch
inteiro le esse mesmo codigo de qualquer forma, e duas revisoes no mesmo diff sao
dois assentos no mesmo trabalho. Os achados da Task 5 foram para o prompt da final
como itens de verificacao obrigatorios. Isso e a alavanca de velocidade que o
usuario aprovou.

Revisao final do Plano 2 despachada (opus), escopo 95f5308..5c99b17 (17 commits).
Base escolhida assim porque tudo antes de 95f5308 e Plano 1, que ja teve a propria
revisao final — o branch carrega os dois planos porque o Plano 1 nunca foi mesclado
na main.

REVISAO FINAL DO PLANO 2 (opus, 95f5308..5c99b17): NAO ENVIAR. Tres Importants
bloqueantes, todos no tree_export.cpp, todos de conserto pequeno.

Bloqueante 1 — os buffers do SnapshotCallback sao escritos por varias threads do
XAML sem lock. reported e to_release sao vector comuns num objeto compartilhado, e
a assinatura fica viva os ~106 ms que o Advise leva (medido no spike,
01:01:11.343 -> .449). Qualquer OUTRA ilha XAML do explorer que mute a arvore
nesse intervalo chama OnVisualTreeChange na thread DELA. Que o callback vem de
mais de uma thread nao e especulacao: e por isso que o upstream declara o
equivalente como thread_local (vendor:18281) e aborta com "Not initialized for
thread %u" (vendor:11112). O upstream fugiu do lock deixando o estado por thread;
nos compartilhamos um objeto e nao pegamos lock nenhum.
Este e o lado ESCRITA da corrida que o proprio implementador nomeou no desvio
dele. Ele fechou o lado leitura (montar/formatar/escrever so depois do Unadvise);
o lado escrita durante o Advise continuou aberto.

Bloqueante 2 — no caminho de falha do Advise o callback e deletado sem nunca
chamar Unadvise. O Unadvise so roda sob SUCCEEDED(advise_hr), mas o comentario do
proprio guard antecipa o caso de o Advise reportar elementos e SO ENTAO devolver
falha. Ali leak_callback e false e o destrutor deleta um objeto que o XAML pode
ainda ter registrado.

Bloqueante 3 — o ~ReleaseOnExit pode lancar, e isso transforma um OOM
sobrevivivel em shell morto. O destrutor e noexcept implicito e alcanca
STYLER_LOG, que monta wstring e tranca mutex. O cenario para o qual o guard FOI
CRIADO e bad_alloc desenrolando do BuildForest: memoria ja esgotada, destrutor
roda durante o desenrolamento, aloca de novo, lanca de novo -> std::terminate ->
explorer morre, passando por cima do catch(...) do SetSite. Achado bonito: o
guard adicionado por seguranca era o proprio risco.

Verificado limpo pela revisao, e vale registrar: contabilidade de handle nos nove
caminhos de saida (paridade exata com vendor:11162-11171), a fronteira, o tempo de
vida do g_session, a ausencia de AcquireSession()->diagnostics() em expressao
unica, e a propriedade de nao-injecao no codigo enviado. O desvio do Unadvise foi
julgado a escolha melhor.

Fix round despachado com os tres bloqueantes mais seis one-liners, dois deles
casos de "a checagem nao checa o que promete": o grep anti-injecao do CI cobre
quatro dos seis nomes que o DoD promete, e nao existe guarda de x64 em lugar
nenhum. §7.1 da spec corrigida por mim em 18d7e45.

Para o Plano 3, herdado desta revisao: SendMessageW sem timeout no
thread_init.cpp:128 chamado de thread de UI; passar o shared_ptr da sessao que o
export ja segura ate o loop de liberacao, em vez de o ReleaseHandle reler o global
(um CloseDiagnostics concorrente hoje faria a batelada inteira virar no-op
silencioso); e cruzar VisualElement::NumChildren com os filhos materializados para
pegar lote parcial, que hoje sai como arvore truncada plausivel com S_OK. E a
assinatura permanente do Plano 3 torna o Bloqueante 1 permanente em vez de uma
janela de 100 ms — a forma a copiar e a fila thread_local + dreno no dispatcher do
upstream (vendor:18281, :18362-18420).

Fix round da revisao final: commit 4880b56. Bloqueantes 2 e 3 FECHADOS, os seis
one-liners FEITOS, Bloqueante 1 PARCIAL.
Re-review (opus): sobra um item, de duas linhas. O mutex cobre toda ESCRITA
corretamente, mas o acquire mora dentro do else — so roda quando o Unadvise deu
certo. No caminho leak_callback (Advise ok, Unadvise falhou) a funcao cai direto
em :251, :255 e :270 lendo callback->reported sem lock, num objeto que por
premissa daquele caminho AINDA esta registrado no XAML. E a janela nao e de
microssegundos: e BuildForest + formatar + escrever arquivo.
Conserto: retornar unadvise_hr logo depois do log de erro em :189, antes de o
guard existir. Deixa o corpo da funcao coerente com a postura que o destrutor ja
tem: vaza o objeto inteiro, nao toca em nada.
O revisor avisou explicitamente para NAO alargar o lock por cima do BuildForest —
o OnVisualTreeChange pega o mesmo mutex de uma thread de UI do explorer, entao
segurar durante formatar-e-escrever trocaria uma corrida rara por um hang de
rotina. Concordo.

Ruling 14 — o item 5 (validacao do alvo no WH_CALLWNDPROC) fica como esta, com o
residual registrado. Minha leitura estava certa e o revisor confirmou: o codigo le
p->target, que e desreferencia do proprio ponteiro sob validacao, em offset 16 de
um endereco nao validado. Mas o residual e so adversarial e praticamente
inalcancavel — exige conhecer a string privada TaskbarStyler_RunOnWindowThread,
acertar a thread alvo, cair dentro de um unico SendMessageW, e passar o UIPI; e
mesmo assim rende access violation, nao controle, porque chegar em p->proc exige
que *(HWND*)(lParam+16) case com o hwnd receptor, o que ja pressupoe controlar a
memoria do explorer. As duas alternativas sao piores: armazenamento proprio exige
mapa sob mutex lido de dentro de um WH_CALLWNDPROC numa thread de UI do explorer
com o chamador bloqueado em SendMessageW, risco de deadlock real; e cookie em
wParam e ficcao contra quem ja sabe o nome da mensagem.
O que MUDA e o relatorio: task-5-report.md:456-461 afirma que a checagem acontece
antes da desreferencia, e nao acontece. Mandei corrigir a frase. O comentario no
codigo ja e honesto e fica. Isso importa mais que o codigo: o Plano 3 alarga
exatamente esta janela, e quem ler aquela linha precisa da propriedade verdadeira,
nao da lisonjeira.
Custo se errado: uma leitura selvagem de 8 bytes num cenario que exige um atacante
que ja leu este codigo-fonte.

Nota do revisor para nao interpretar errado depois: ReleasedHandleCount e
cumulativo do processo. A conta 681 = 341 Add x 2 - 1 no-op do pai zero da raiz so
fecha no PRIMEIRO export; um segundo load no mesmo explorer imprime ~1362, e isso
nao e vazamento.

Fix round 3 da revisao final, feito por mim: o implementador (sonnet) caiu no
limite de sessao do usuario no meio da rodada, sem deixar nada no working tree.
A correcao era de duas linhas com prescricao exata do revisor, entao apliquei
direto em vez de despachar de novo: return unadvise_hr logo depois do log de
erro, antes de o ReleaseOnExit existir; o guard perdeu o campo leak_callback e
o early return (-13 linhas liquidas). Relatorio da Task 5 corrigido no item 5
(a checagem do WH_CALLWNDPROC gateia a CHAMADA, nao a leitura) e com a secao
"Fix round 3" no fim.
Verificacao minha, nao delegada: build limpo (so tree_export.cpp recompilou),
ctest 2/2, smoke ao vivo num explorer recem-iniciado (pid 7900): "snapshot: 559
elements", "tree exported", "handles released so far: 1114". Aritmetica fecha:
559 x 2 = 1118, menos 4 no-ops de pai zero = 4 raizes (varios hosts XAML nesta
corrida; a anterior tinha 1 raiz e 341 elementos). A cadeia real esta la:
Taskbar.TaskbarFrame#TaskbarFrame > Grid#RootGrid >
Taskbar.TaskbarBackground#BackgroundControl > Grid > Rectangle#BackgroundFill[1].

Ruling 15 — sem re-review desta rodada. O processo preve UM fix dispatch e UMA
re-review escopada depois da revisao final, e as duas ja aconteceram. O que
sobrou foi um item cuja correcao o proprio revisor prescreveu linha a linha, e
eu apliquei exatamente aquela prescricao, com delta negativo. Mais um assento de
revisao para conferir -13 linhas que o revisor escreveu nao compra nada.
Custo se errado: um erro de transcricao meu num caminho (Unadvise falhando) que
nunca foi observado ao vivo e que agora so faz return.

PLANO 2: COMPLETO. Veredito da revisao final satisfeito ("fix the report wording
alongside the two-line change and this ships").

Herdado pelo Plano 3, consolidado:
- Assinatura permanente + fila thread_local + dreno no dispatcher (vendor:18281,
  :18362-18420). O Bloqueante 1 da revisao final vira permanente com assinatura
  de pe.
- Passar o shared_ptr<DiagnosticsSession> que o export segura ate o loop de
  liberacao, em vez de ReleaseHandle reler o global.
- SendMessageW sem timeout no thread_init.cpp (SendMessageTimeoutW +
  SMTO_ABORTIFHUNG).
- Cruzar VisualElement::NumChildren com filhos materializados (lote parcial hoje
  sai como arvore truncada plausivel com S_OK).
- Matcher precisa expandir nomes curtos do JSON dos temas para runtime class
  names completos (AdjustTypeName, vendor:18795-18801).
- Medidor de handles VIVOS da spec §7.2 (nao implementado no Plano 2).
- Residual do WH_CALLWNDPROC (Ruling 14): o Plano 3 alarga a janela.
- Constantes/variaveis de recurso/matcher puro no styler_core (ja previstos).
