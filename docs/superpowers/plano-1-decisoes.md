# SDD ledger — plan: docs/superpowers/plans/2026-09-12-plano-1-nucleo-e-temas.md

Branch: `plano-1-nucleo-e-temas`
Base: `c0953a1`
Spec: `docs/superpowers/specs/2026-09-12-taskbar-styler-design.md`

## Pre-flight scan

### Pares de tarefas que compartilham arquivo ou interface

| A | B | Produz → Consome | Achado |
|---|---|---|---|
| T1 | T2, T3, T4 | `src/core/CMakeLists.txt` criado → cada uma acrescenta um .cpp | OK, append puro |
| T1 | T2, T3, T4, T7 | `tests/core/CMakeLists.txt` criado → cada uma acrescenta um teste | OK, append puro |
| T1 | T5, T6, T7 | `.github/workflows/ci.yml` criado → cada uma acrescenta um passo | OK, append puro |
| T1 | T4 | `nlohmann_json` PRIVATE em styler_core → `theme_loader.cpp` inclui | OK, header público não expõe nlohmann |
| T2 | T3 | `ParseError` → incluído via `styler/selector.h` | OK |
| T2 | T4 | `ParseSelector`, `ElementMatcher` → `ThemeRule.selector` | OK, assinaturas batem |
| T3 | T4 | `ValueRule`/`CaptureRule`/`StyleRule`/`ParseStyleRule` → `ThemeRule.styles` | OK |
| T4 | T7 | `LoadThemeFromFile` → `test_corpus.cpp` | OK |
| T5 | T6 | `ThemeTable.span` → `emit_theme_table` compara com `text[start:end]` | OK |
| T5 | T7 | CLI `convert --credits` → `fetch_theme_credits.py` gera `credits.json` | OK |
| T5 | T7 | id do Squircle não selecionável → `osFeatureVariant.themeId` | OK, ambos `Squircle_WeatherOnTheRight` |
| T7 | — | `STYLER_THEMES_DIR` definido por CMake → `test_corpus.cpp` | OK |

### Auto-consistência de cada tarefa

| Tarefa | Achado |
|---|---|
| T1 | OK — 20409 linhas verificado contra o upstream |
| T2 | OK — testes cobrem a gramática e as 8 mensagens de erro do upstream |
| T3 | OK — testes cobrem `:=`, `@`, `=>` e as combinações proibidas |
| T4 | **CONFLITO** — `CheckConstantReferences` contradiz a semântica do upstream (ver Ruling 1) |
| T5 | OK — mas `test_rejects_a_duplicated_constant` casa string com aspas escapadas; frágil, não bloqueante |
| T6 | OK — o passo 4 já avisa que vai falhar e manda ajustar o emissor, não o teste |
| T7 | OK — 55 e 2396 verificados contra o upstream |
| T8 | OK — sem dependências |

### Rulings

**Ruling 1 — remover `CheckConstantReferences` da Task 4.**
Evidência: `ApplyStyleConstants` no upstream
(`vendor/upstream/windows-11-taskbar-styler.wh.cpp:17958`) faz substituição de
substring por prefixo em **qualquer posição** do valor, e deixa `$` sem
correspondência passar como **literal** — nunca lança.
Medição sobre os dados reais: das 1567 strings com `$`, 85 têm a referência
embutida no meio do valor; e 10 referências não resolvem contra constante
alguma do próprio tema (`$WidgetGap57`, `$Hover`), afetando os temas
`Luminosity_variant_Dock`, `Luminosity_variant_Compact` e `Fluid`.
Um check que lança rejeitaria 3 temas válidos e derrubaria o teste de corpus
da Task 7.
Decisão: o carregador valida forma do JSON, sintaxe de seletor e sintaxe de
regra — **não** resolução de constante. A resolução é preocupação de runtime e
pertence ao TAP (Plano 2), onde deve replicar a semântica do upstream.
Custo se errado: um `$Nome` digitado errado renderiza como literal em vez de
falhar alto — exatamente o comportamento do mod, então sem regressão.

**Ruling 2 — nome de arquivo do tema é sanitizado; o id autoritativo vive no JSON.**
`Oversimplified&Accentuated` é um id selecionável e viraria
`themes/Oversimplified&Accentuated.json`. O `&` é legal no Windows mas pesa em
shell e CI.
Decisão: o arquivo usa o id com tudo fora de `[A-Za-z0-9_.-]` trocado por `_`;
o campo `"id"` dentro do JSON mantém o id verdadeiro, e é ele que o corpus e o
Tray consultam.
Custo se errado: divergência cosmética entre nome de arquivo e id ao navegar
em `themes/`.

## Progresso

Task 1: implementado (commit 0ac1544), status DONE_WITH_CONCERNS — desvio do
brief: `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` para o CMake 4.3.1 aceitar o
doctest 2.4.11.

Task 1: revisao — spec OK, qualidade aprovada com 1 Important:
a variavel de politica e de escopo de diretorio e nunca e desfeita, entao
vazaria para qualquer FetchContent futuro. Entra no loop.

Task 1: Ruling 3 — SPDX so em codigo (.h/.cpp/.py), nao em build/CI.
A obrigacao de aviso da GPL ja e cumprida por LICENSE, NOTICE e os cabecalhos
do codigo; espalhar por CMakeLists e YAML e ruido. Global Constraints do plano
atualizado para nao deixar as proximas tarefas oscilarem.
Custo se errado: inconsistencia cosmetica; corrigivel em massa depois.

Task 1: Ruling 4 — a lista "Files" da Task 1 no plano omitia src/core/version.cpp,
que o Step 3 exige. Defeito de redacao do plano, nao do implementador.
Plano corrigido.
Custo se errado: nenhum, ja implementado corretamente.

Task 1: fix round 1/5 (1 addressed, 0 open — CMAKE_POLICY_VERSION_MINIMUM escopado
ao fetch do doctest com unset logo apos; commits 0ac1544..25cf3f0)
Task 1: complete (commits 7fc7dbb..25cf3f0, review clean)

Task 2: implementado (commit e8dd0eb), 11 TEST_CASE, transcricao fiel do brief.
Task 2: revisao — spec OK, qualidade NAO aprovada. 1 Important (relatorio afirma
13 test cases, numero errado e breakdown que soma 16); 2 minors herdados do
proprio brief; 1 aviso sobre ParseSelector.

Task 2: minor (deferido): include <charconv> nao usado em src/core/selector.cpp
(ParseIndex soma digitos a mao). Herdado do texto do brief.
Task 2: minor (deferido): ParseIndex sem guarda de overflow; o std::stoi do
upstream ao menos lanca out_of_range. Indices reais de arvore visual nao chegam
perto do limite.

Task 2: Ruling 5 — ParseSelector passa a dividir em '>' apenas com profundidade
zero de colchetes, divergindo deliberadamente do upstream.
Evidencia medida sobre os 2396 seletores reais do arquivo vendorizado: zero tem
'>' sem espacos ao redor, zero tem '>' dentro de colchetes; as duas estrategias
produzem saida identica em todos os temas embarcados. Portanto a mudanca nao
altera comportamento algum hoje.
Razao: o " > " do upstream (vendor:18859) faz um `Grid>Rectangle` escrito a mao
nunca casar, silenciosamente; o '>' cru do brief quebra `Grid[Tag=A>B]`. Temas
editados a mao sao objetivo central deste projeto, ao contrario do upstream onde
os temas sao compilados.
Custo se errado: nenhum sobre os temas atuais, provado pelo teste de corpus da
Task 7; no pior caso um seletor exotico escrito a mao se comporta diferente do
Windhawk.

Task 2: Ruling 6 — numeros de linha do upstream no plano e na spec estavam
obsoletos: vieram da copia truncada do usuario (19831 linhas), nao do arquivo
vendorizado (20409). Corrigidos: ElementMatcherFromString 18051 -> 18628,
ParseRule 18150 -> 18727, ApplyStyleConstants 17958 -> 18536.
Custo se errado: referencia errada faz o implementador ler a funcao errada.

Task 2: fix round 1/5 (2 addressed, 0 open — relatorio corrigido para 13 test
cases reais; ParseSelector com rastreio de profundidade de colchetes; commits
e8dd0eb..6243d3a)
Task 2: minor (deferido): profundidade de colchetes pode ficar negativa em
entrada malformada (`Grid]>Rectangle`); o guard e `== 0`, nao `<= 0`. So afeta
seletor ja invalido, nenhum teste cobre.
Task 2: complete (commits b226766..6243d3a, review clean)

Task 3: implementado (commit 710fe84), 9 TEST_CASE novos, 23 no total.
Task 3: revisao — spec OK, qualidade aprovada. 2 minors, ambos herdados do brief
e apontando para a mesma causa.
Task 3: complete (commits a70c875..710fe84, review clean)

Task 3: Ruling 7 — consolidar Trim em src/core/detail/text.h, com o conjunto de
espacos do upstream `L" \t\r\v\n"`, executado DENTRO da Task 4.
Motivo: selector.cpp e style_rule.cpp tem copias identicas de kWhitespace/Trim,
ambas omitindo o \v que o TrimStringView do upstream (vendor:15117) inclui. A
Task 4 criaria a terceira copia. Consolidar depois de tres copias e refatoracao;
consolidar agora e evitar que a divergencia se espalhe.
Custo se errado: um valor de estilo contendo tabulacao vertical seria aparado
diferente do Windhawk. Probabilidade baixissima, mas a correcao e barata e o
principio do projeto e fidelidade ao upstream.

Task 4: implementado (commits 06b0eea refactor Trim, e306260 tema/utf/loader),
31 TEST_CASE. Trim consolidado em src/core/detail/text.h com o \v do upstream.
Task 4: revisao (opus) — spec OK, qualidade aprovada, mas 3 Important e 8 minors.

Task 4: Ruling 8 — elevar o Finding B (overlong UTF-8) de Minor para Important.
Razao: `"\xC0\x80"` decodifica para U+0000, injetando NUL embutido numa string
que vira nome/valor de propriedade XAML, onde o Win32 trunca. Contradiz o
contrato de falhar fechado do carregador, e o mesmo guard que corrige o Finding A
corrige este — separa-los seria artificial.
Custo se errado: uma rodada de fix a mais do que o processo estritamente exigia.

Task 4: minor (deferido): utf.cpp — WideToUtf8 emite WTF-8 para surrogate solto
em vez de U+FFFD; assimetrico com Utf8ToWide. So importa quando existir um
escritor de temas.
Task 4: minor (deferido): utf.cpp — sequencia truncada gera dois U+FFFD onde a
recomendacao de maximal subpart do Unicode da um.
Task 4: minor (deferido): theme_loader.cpp — featureId estreita silenciosamente
de uint64 para uint32; 4294967296 vira 0 em vez de ParseError.
Task 4: minor (deferido): theme_loader.cpp — json::parse descarta o diagnostico
de offset/linha do nlohmann; todo erro de sintaxe vira "Theme is not a JSON
object". Temas sao editados a mao, a mensagem importa.
Task 4: minor (deferido): theme_loader.cpp — campo `author` falha aberto (um
author nao-string e ignorado) enquanto todo o resto falha fechado.
Task 4: minor (deferido): falta /utf-8 no MSVC; test_theme_loader.cpp e UTF-8
sem BOM com literais nao-ASCII. Passa hoje so porque source e execution charset
coincidem.
Task 4: minor (deferido): /W4 /permissive- aplicado so a styler_core, nao ao
alvo de testes.
Task 4: minor (deferido): utf.cpp — include <cstdint> nao usado.
Task 4: minor (deferido): o teste de \v cobre selector.cpp; style_rule.cpp tambem
mudou de comportamento de trim e nao ganhou teste de \v.

Task 4: fix round 1/5 (4 addressed, 0 open — guard de escalar UTF-8 mal formado
com as tres classes; test_utf.cpp novo com 6 casos; LoadThemeFromFile coberto via
STYLER_TEST_DATA_DIR; passagem falsa do relatorio corrigida; commit 08ad220)

Task 4: Ruling 9 — nao abrir nova rodada pela frase obsoleta em
task-4-report.md:265, que ainda diz a Task 5 que LoadThemeFromFile nao tem teste.
Razao: o arquivo de relatorio e efemero (o workspace e apagado no fim do plano) e
quem redige o despacho da Task 5 sou eu, entao a afirmacao errada nao chega a
ninguem. Uma rodada de fix por uma frase em arquivo descartavel nao se paga.
Custo se errado: se alguem ler o relatorio antes do descarte, le uma frase falsa.

Task 4: minor (deferido): a classe surrogate do guard (ED A0 80 -> U+FFFD) nao
tem teste; as quatro entradas cobrem overlong e fora de faixa.
Task 4: minor (deferido): o teste chamado "bad continuation byte" na verdade
exercita o ramo de truncamento; o caminho extra = k - 1 segue sem cobertura.
Nome e comentario descrevem errado o que ele prova.
Task 4: minor (deferido): WideToUtf8 emite CESU-8 (ED A0 80) para surrogate
solto, bytes que o proprio decodificador agora rejeita; test_utf.cpp consagrou
isso como esperado. Merece decisao explicita, nao invariante acidental.
Task 4: minor (deferido): nao conforma com a recomendacao de maximal subpart do
Unicode (um U+FFFD para sequencia mal formada de 3 bytes, nao um por subparte).
Task 4: minor (deferido): test_utf.cpp usa std::initializer_list sem incluir
<initializer_list>; funciona via <string> no MSVC.
Task 4: complete (commits 710fe84..08ad220, review clean)

Task 5: implementado (commit 739490d), 8 testes pytest, 55 arquivos / 2396
regras / 55 ids verificados por mim de forma independente.
Task 5: revisao (opus) — spec OK, qualidade NAO aprovada. 1 Critical, 2 Important,
4 minors. As duas divergencias do implementador foram aceitas: o `@` no regex de
chave cobre 22 chaves reais (Accent1@Dark etc.), e o exact-match-first do
selectable_ids vale para todos os 54 ramos com 190 chars de folga na janela.

Task 5: Critical C1 — _read_wide_literals corrompia \uXXXX em 43 literais de 18
temas (\uE971 virava o texto "uE971"). Selector corrompido nunca casa; valor
corrompido renderiza o texto literal na taskbar. Entra no loop.

Task 5: Ruling 10 — NAO aparar valores de constante/variavel de recurso, apesar
do upstream aparar (64 valores afetados, guardamos " 8" onde ele guarda "8").
Razao: o contrato do JSON e transliteracao fiel da fonte, e e exatamente isso que
torna possivel o round-trip byte a byte da Task 6; aparar destroi a informacao
necessaria para reconstruir o original. Aparar pertence a resolucao de constante,
que a Ruling 1 ja atribuiu ao TAP no Plano 2.
Custo se errado: se o TAP esquecer de aparar na resolucao, um valor sai com
espaco a esquerda. Mitigado por comentario no ponto do split.

Task 5: Ruling 11 — regra de escape para o round-trip, medida sobre os 55 spans:
existem exatamente tres formas de escape na regiao dos temas (\" x6070, \ x102,
\uXXXX x43, todos com 4 hex maiusculos) e ZERO nao-ASCII cru. Portanto o emit da
Task 6 deve escapar \ como \, " como \", e todo char com ord>127 como \u mais 4
hex MAIUSCULOS, e nada mais. Isso reproduz a fonte exatamente.
Custo se errado: o round-trip da Task 6 falha e a divergencia aparece no diff —
ou seja, o erro se denuncia sozinho.

Task 5: minor (deferido): fallback "primeiro ponteiro vence" do selectable_ids
segue sem guarda; nunca dispara hoje porque o invariante de exact-match vale para
os 54 ramos, mas adivinha em silencio se disparar.
Task 5: minor (deferido): o ramo exact-match nao valida que o struct nomeado
existe na saida de parse_source.
Task 5: minor (deferido): o regex de chave rejeita duas formas que o upstream
suporta e nao ocorrem hoje (chave com dois-pontos final, entrada comentada com
//). Falha alto, que e aceitavel, mas merece comentario.
Task 5: minor (deferido): sobras cosmeticas do brief — THEME_START aceita & em
identificador C++, docstring anuncia o modo emit que so chega na Task 6,
parametro `name` de to_theme_json nao usado.

Task 5: fix round 1/5 (2 addressed, 1 open — C1 corrigido com decodificacao de
\uXXXX e raise em escape desconhecido; I2 --expect-count entregue; I1 NAO
resolvido: o fixture de test_decodes_uxxxx_escapes contem o caractere U+E971
literal, sem barra invertida alguma, entao o ramo \u nunca e exercitado e
reverter o bug Critical deixa 16/16 verdes; commits 739490d..c58a58d)

Task 5: Ruling 12 — puxar para dentro do loop o teste que guarda a Ruling 10
(nao aparar valores), listado como deferido pelo revisor.
Razao: a mutacao `value.strip()` sobrevive aos 16 testes e violaria em silencio a
decisao da qual a Task 6 depende; a falha apareceria uma tarefa depois, parecendo
bug da Task 6. Comentario nao e guarda.
Custo se errado: um teste a mais numa rodada que ja estava aberta por outro
motivo, portanto praticamente zero.

Task 5: a regra de escape da Ruling 11 foi re-derivada de forma independente pelo
revisor: 9933 literais nos 55 spans, censo identico ao meu (6070/102/43, todos
4 hex maiusculos, zero nao-ASCII cru), zero divergencias no re-escape. A Task 6
pode confiar nela.
Task 5: nota para a Task 6 — FORA dos 55 spans o arquivo tem \n x40, \t x2,
\r x2, \v x2 em literais L"...". O raise so e seguro porque _read_wide_literals
roda apenas dentro dos spans. Ampliar a regiao de varredura quebra.

Task 5: minor (deferido): a preferencia exact-match do selectable_ids e codigo
morto, dado que o fallback e ancorado em ';'.
Task 5: minor (deferido): erro do --expect-count vai para stdout, nao stderr, e
nao tem teste de CLI.

Task 5: fix round 2/5 (2 addressed, 0 open — fixture do \uXXXX reconstruido com
barra invertida real mais asercao de sanidade; teste do no-trim contra o
mainRadius = 8 real do g_themeMatter; ambas as mutacoes verificadas mortas por
observacao direta pelo re-revisor; commits c58a58d..478f8b0)
Task 5: complete (commits 08ad220..478f8b0, review clean, 17 testes pytest)

Task 6: implementado (commit 59a8900). ROUND-TRIP FECHADO: 55 temas reconstruidos
byte a byte, 19 testes pytest, passo no CI.
Task 6: revisao (opus) — spec OK, qualidade aprovada, 1 Important, 2 minors.
Verificado empiricamente que o round-trip TERIA pego o bug Critical da Task 5:
reintroduzido o bug numa copia, saiu exit 1 apontando g_themeRosePine com o diff
exato. Tambem detecta literal solto removido de uma lista de estilos e troca
entre constants e resource_variables.

Task 6: Important — cmd_roundtrip nao tem a guarda --expect-count que o
cmd_convert tem. Com `const Theme g_theme` -> `static const Theme g_theme`, ele
imprime "OK: 0 themes reconstructed byte for byte" e sai 0. Entra no loop.

Task 6: Ruling 13 — puxar para o loop o teste hermetico do \uXXXX, listado como
Minor. Razao: o ramo nao-ASCII do _escape_wide implementa a Ruling 11, medida e
load-bearing, e hoje so e exercitado atraves do arquivo vendorizado. Uma regra
central do projeto nao deve depender de vendor/ estar presente e intacto.
Custo se errado: um teste a mais numa rodada ja aberta.

Task 6: LIMITE DA PROVA, registrado para nao se perder — um par identidade
(_read_wide_literals devolvendo o texto cru e _escape_wide como `return s`)
tambem passa com 55/55. `emit(parse(x)) == x` prova que o par e uma bijecao, NAO
que o JSON intermediario esta semanticamente certo. Quem fecha esse buraco sao os
testes test_decodes_uxxxx_escapes e test_unescapes_embedded_quotes da Task 5.
Ninguem deve apaga-los achando que o round-trip os substitui.

Task 6: adjudicado o alerta do implementador sobre bloco final vazio vs ausente:
gap latente so para a Task 7, nao defeito da Task 6. O emit acerta onde da para
recuperar (bloco constants vazio seguido de resourceVariables cheio round-trippa
exato) e falha alto onde nao da. Carregar para o brief da Task 7.
Task 6: minor (deferido): subject do commit em portugues, destoando dos demais.

Task 6: fix round 1/5 (2 addressed, 0 open — --expect-count no cmd_roundtrip,
verificado por mutacao: com THEME_START quebrado sai "expected 55, found 0" exit
1 onde antes saia "OK: 0" exit 0; teste hermetico do escape que morre com
{:04x}; commits 59a8900..24f3a0f)
Task 6: complete (commits 478f8b0..24f3a0f, review clean, 22 testes pytest)

Task 7: implementado (commit 23cea53) — 55 temas, credits.json (43 de 55 autores),
THEMES.md, teste de corpus 55/2396, passo de drift no CI. MAS o commit trouxe
junto mudancas nao pedidas em src/core/ vindas de fora do brief.
Task 7: revisao (opus) — spec NAO OK, qualidade NAO aprovada. 4 Important.

Task 7: DEFEITO DO PLANO (meu) — nenhuma das 8 tarefas previu targets separados
por virgula. O upstream tem SplitTargetString (vendor:18825), ciente de colchetes,
e AddElementCustomizationRules aplica os mesmos estilos a cada cadeia. 624 das
2396 regras sao multi-cadeia. A mudanca de ThemeRule::selector para
vector<vector<ElementMatcher>> era necessaria; o port do splitter foi verificado
fiel caractere a caractere.

Task 7: Ruling 14 — isolamento de erro por cadeia, nao por regra. O catch do
upstream e por parte do target; o nosso descarta todas as cadeias quando uma e
ambigua, o que para 624 regras multi-cadeia jogaria fora cadeias boas.

Task 7: Ruling 15 — estilo vazio passa a espelhar o upstream (mata os estilos
daquela regra) em vez de ser pulado em silencio. Falhar o tema inteiro rejeitaria
o LiquidGlass2, e limpar no extrator quebraria o round-trip byte a byte.
Custo se errado: uma regra a menos aplicada num tema que hoje ja nao a aplica.

Task 7: Ruling 16 — acabar com o sentinela "vector vazio = regra morta". Passa a
ter ThemeRule::dead explicito e Theme::diagnostics. Um container vazio le-se tao
naturalmente como "sem restricoes, casa com tudo", e o unico caso real e um
Background:=Red que assim seria pintado sobre a taskbar inteira. Alem disso a
§7.6 exige *reportar*, e hoje nao ha canal de diagnostico algum.
Custo se errado: um campo a mais no modelo que o Plano 2 precisa consultar.

Task 7: Ruling 17 — spec §7.6 emendada com as duas excecoes que sobrevivem, em
vez de deixa-las como comentario no codigo. Excecao de spec decidida em codigo e
excecao que ninguem revisa.

Task 7: minor (deferido): THEMES.md linka 12 linhas sem autor para READMEs que
nao existem; sem legenda para "—".
Task 7: minor (deferido): fetch_theme_credits.py apaga os 43 creditos em silencio
se rodar durante queda de rede; credits.json esta fora do drift check do CI.
Task 7: minor (deferido): o ramo `if r.status != 200` e codigo morto, urlopen
nunca devolve nao-2xx.
Task 7: PRE-EXISTENTE (deferido para a revisao final): as validacoes de cadeia que
o upstream faz em AddElementCustomizationRulesForSingleTarget (vendor:18871-18917)
nao existem no nosso selector — '*' nao pode ser o elemento casado, nem o mais a
esquerda, nem adjacente a outro '*'; ':root' tem de ser o mais a esquerda; no
maximo um grupo de estado visual por cadeia. A §7.6 afirma paridade em rejeitar
seletor invalido, entao isso e uma lacuna real de paridade.

Task 7: fix round 1/5 (6 addressed) — isolamento por cadeia dentro de
ParseSelectorGroups; estilo vazio espelhando upstream; ThemeRule::dead e
Theme::diagnostics; pins exatos no corpus; spec §7.6 emendada; minors de
THEMES.md e fetch_theme_credits. Commit 6c76e7c.
Task 7: fix round 2/5 (4 addressed, 0 open) — dois comentarios introduzidos pelo
proprio fix afirmavam diagnostics 1:1 com dead (falso, o drop parcial de cadeia
emite diagnostico sem marcar dead); spec §7.6 prometia tolerancia mais ampla que
o codigo; o break pulava a checagem de tipo das entradas seguintes; dead passou a
|=. Commit c4ee3ac.
Task 7: complete (commits 24f3a0f..c4ee3ac, review clean, 55 casos / 290
assercoes C++, 22 pytest, corpus 55/2396/5 dead/5 diagnosticos)

Task 8: implementado (commit 1e64a26) — LICENSE GPL-3.0 (35 kB), NOTICE com
atribuicao a m417z e aos autores de tema, README.
Task 8: revisao dobrada na revisao final da branch, em vez de rodada propria:
sao tres arquivos estaticos que o revisor final le de qualquer forma.
Task 8: complete (commits c4ee3ac..1e64a26)

## Revisao final da branch (opus) — READY WITH FIXES, 10 itens

Task 1: Ruling 10 CORRIGIDA — eu estava errado, e a justificativa era falsa.
Eu disse que aparar valores de constante quebraria o round-trip. Nao quebra:
emit_theme_table le table.constants, a lista CRUA de literais; o JSON passa por
_split_pairs; o round-trip nunca cruza os dois. Aparar muda zero bytes na prova,
e foi confirmado depois da mudanca (55 temas byte a byte seguem OK).
Pior, o contrato que eu dizia proteger ja estava quebrado assimetricamente: a
chave era aparada e o valor nao, em 64 valores de 8 temas.
Decisao correta: aparar o valor, como o upstream faz nos dois lados.
Licao: justifiquei uma decisao com uma afirmacao tecnica que nao verifiquei.

Task 1: Ruling 1 CORRIGIDA — a resolucao de constantes acontece em tempo de
aplicacao, mas implementada em styler_core e chamada pelo TAP, nao dentro do TAP.
A §4.3 atribui "a resolucao de constantes" ao core e diz que logica crescendo no
TAP e sinal de que deveria estar no core. ApplyStyleConstants e substituicao pura
de string, a funcao mais testavel do mod inteiro; joga-la numa DLL COM que a
propria §4.3 marca como nao testavel e o exato modo de falha que a secao existe
para evitar.

Onda de correcao final (F1-F10): trim de valores; guarda de overflow no
ParseIndex e profundidade <= 0; as 6 validacoes de cadeia do upstream (medido:
0 de 3123 cadeias violam, entao nenhum pin se moveu); tres buracos fail-open no
loader (author, featureId, diagnostico do nlohmann); /utf-8 e /W4 no alvo de
teste; dependencias presas em SHA completo; deriva de nomes entre spec e
ferramenta; PROVENANCE do arquivo vendorizado.
Verificado por mim: 22 pytest, roundtrip 55 byte a byte, 68 casos / 313
assercoes, pins do corpus inalterados (55/2396/5/1/4/5).

Historico: commit 710fe84 tem trailer colado no subject. NAO reescrever — o
ledger cita 9 SHAs e o rastro de auditoria vale mais que a cosmetica.
