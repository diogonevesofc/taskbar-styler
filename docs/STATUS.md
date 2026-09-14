# Estado do trabalho

Atualizado em 2026-09-14. Fonte de verdade para "onde estamos" (o ledger de
execução dos agentes fica em `.superpowers/`, fora do git).

## Planos

| Plano | Escopo | Estado |
|---|---|---|
| 1 | `styler_core`: seletores, regras, temas; 55 temas convertidos byte a byte | mesclado em `main` |
| 2 | TAP carrega no explorer; exporta a árvore visual | mesclado em `main` |
| 3 | Aplicar e desfazer estilos; assinatura permanente; CLI `apply/reset` | mesclado em `main` |
| 3b | Fidelidade: blur real, variáveis de estilo, reciclagem, timeout no fan-out | **em execução**, branch `plano-3b-fidelidade` |
| 4 | App de bandeja em C# .NET 10, sempre ligado, sobrevive a restart do explorer | não iniciado |

`main` = `be0d3e6` (em `origin`). Branch `plano-3b-fidelidade` também em `origin`
até `6c68ec6`; commits posteriores são locais até novo push autorizado.

## Plano 3b — `docs/superpowers/plans/2026-09-14-plano-3b-fidelidade.md`

| Task | O quê | Estado | Commits |
|---|---|---|---|
| 1 | `SendMessageTimeoutW` no fan-out; `NumChildren` cruzado com filhos entregues | concluída, revisada | `573b8ca`, `b980ba5` |
| 2 | `BlurSpec` + `ParseWindhawkBlur` no core; `AcrylicBrush` vira fallback | concluída, revisada | `978fd63` |
| 3 | Cinco efeitos D2D sobre `IGraphicsEffectD2D1Interop` do SDK + ruído | concluída, revisada | `943a5c8`, `6c68ec6` |
| 4 | `XamlBlurBrush` por elemento; smoke ao vivo (0 ERR, 0 fallback, blur visível) | commitada, **em revisão** | `857a493` |
| 5 | Avaliador de `{{...}}` no core (puro, 11 casos + corpus) | pendente | — |
| 6 | Capturas `Prop=>Var` e valores dinâmicos no TAP | pendente | — |
| 7 | Reciclagem do `ItemsRepeater` — spike primeiro; pode sair sem código | pendente | — |

Depois da Task 7: revisão final do branch inteiro, uma rodada de correção,
merge local em `main`, `docs/superpowers/plano-3b-decisoes.md` com o ledger.

### Suítes (último estado verde)

core 114/114 · tap 24/24 · 0 warnings novos.

### Decisões tomadas na execução (divergências do plano, todas justificadas)

- T1: `RunParam` no heap, liberado só quando `SendMessageTimeoutW` confirma;
  vazamento deliberado no timeout (hook pode seguir lendo após `Unhook`).
- T2: contagem de blur por fonte distinta (constante ou estilo inline), não por
  uso; corpus mede **269** specs = 272 (grep) − 8 typos `<<WindhawkBlur` do
  upstream + 5 constantes-alias. `PreparedStyle::blur` é a verdade para "esse
  estilo ganha brush"; os contadores são só log.
- T3: `#define INITGUID` só em `blur_effects.cpp` (os `CLSID_D2D1*` são
  `DEFINE_GUID`; `DECLSPEC_SELECTANY`, o linker funde se outro TU definir).
  `AlphaMode` mapeia `DIRECT`; os cinco efeitos têm nome padrão igual ao upstream.
- T4: grão de ruído com `NoiseOpacity="0.1"` (valor dos temas) é sutil demais
  para ver a olho — medido A/B, escala com n², igual ao upstream; não é defeito.
  Uma linha `Debug` por rebuild do brush foi acrescentada em `RefreshBrush`.
- Fatos corrigidos no plano: `ShouldUseFallback` está em `vendor:13843`;
  o lookup elemento→id na T7 deve fazer QI por `IInspectable` como o upstream
  (`vendor:10961`), não `get_abi(FrameworkElement)`.

### Em investigação

- Explorer caiu às 14:13:13 de 2026-09-14 (Winlogon 1002, "o shell parou
  repentinamente"), ~3 min após o smoke da T4 terminar em `reset`, 100 ms
  depois de `new XAML host` no log do TAP. Sem comando dos agentes. **Sem
  relatório WER de explorer e sem Application Error 1000 hoje** — não há dump
  provando falha na nossa DLL, mas a proximidade com `new XAML host` é
  suspeita. Reproduzir (aplicar tema, deixar parado, abrir/fechar hosts) antes
  de fechar a T4; se for o TAP em repouso, é regressão grave.

### Pendências parqueadas (pegar na task indicada ou na revisão final)

- Para a T5 (toca `matcher`): comentário de `test_blur.cpp:124` lista 4
  constantes-alias, são 5 (falta `FrostedAcrylic`); três convenções de trim
  diferentes (`blur.cpp`, `blur_rewrite.cpp`, `matcher.cpp:280`) e a flag
  `rewritten` descartada em `matcher.cpp:267`; prefix matcher duplicado em
  `blur.cpp:233`/`blur_rewrite.cpp:46`; comentário de `matcher.h:47`; includes
  não usados em `blur.cpp` e `test_blur.cpp`.
- Para a revisão final: `DetachSource` poderia usar `copy_to_abi`; `StoreAsync`
  com status ≠ `Started` é ignorado; custo de compilação dos headers de
  composition em `winrt_common.h` para todos os TUs.
- Herdado pelo Plano 4: contador de handles VIVOS (spec §7.2); deadlock
  potencial em `SetSite(nullptr)` (`UnregisterWaitEx(INVALID_HANDLE_VALUE)` +
  ramo `!site` ignorando `Deferred`).

### Smoke manual ainda não feito pelo usuário

`docs/smoke-test.md` itens 7–9: segundo monitor; Iniciar/central de
notificações com tema aplicado; 10 minutos parado conferindo `held`.

## Branches obsoletos (podem ser apagados)

`plano-1-nucleo-e-temas`, `spike-pull-walk`, `spike-standing-crash`.
