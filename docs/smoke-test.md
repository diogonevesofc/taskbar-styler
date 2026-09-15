# Checklist de smoke test

Um runner de CI não tem taskbar interativa, então o que segue é manual e
versionado. Rode antes de fechar qualquer plano que mexa no TAP.

Compile primeiro: `cmake --build build`.

## 1. Carga

- [ ] `taskbar-styler load` imprime `carregado via VisualDiagConnection<n>`
- [ ] `%LOCALAPPDATA%\TaskbarStyler\log.txt` contém `SetSite`
- [ ] a linha `loaded into` aponta para `C:\WINDOWS\Explorer.EXE`
- [ ] o PID nas linhas de log é o mesmo que o comando imprimiu
- [ ] **não** aparece `IXamlDiagnosticsTestHooks unavailable`

## 2. Árvore visual

- [ ] `%LOCALAPPDATA%\TaskbarStyler\visual-tree.txt` existe
- [ ] a cadeia
      `Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill`,
      tirada de `themes/TranslucentTaskbar.json`, aparece aninhada nessa ordem
- [ ] a indentação cresce dois espaços por nível

## 3. Múltiplos hosts

- [ ] o log tem `initialized for thread` para a UI principal
- [ ] tem `host watch started`
- [ ] abrir o flyout de rede produz `new XAML host` e mais um
      `initialized for thread`
- [ ] conectar um segundo monitor depois da carga produz mais um host

## 4. Estabilidade

- [ ] a taskbar continua respondendo normalmente
- [ ] nenhum diálogo aparece
- [ ] após dez minutos de uso, a memória do `explorer.exe` no Gerenciador de
      Tarefas não cresce continuamente

## 5. Limpeza

- [ ] reiniciar o Explorador pelo Gerenciador de Tarefas descarrega a DLL
- [ ] `taskbar-styler status` depois disso não trava nem mente

## Aplicar e desfazer (Plano 3)

Pré-requisito: `cmake --build build`; Explorer reiniciado se a DLL estava carregada.

1. `build\src\cli\taskbar-styler.exe list` — imprime 55 temas.
2. `apply TranslucentTaskbar` — carrega o TAP; a linha `Rectangle#BackgroundStroke`
   some e o fundo muda. Log: `theme TranslucentTaskbar: … prepared`,
   `apply (as of first drain): … 0 failed`.
3. `apply Lucent` (sem reiniciar o Explorer) — log `reload requested` …
   `restored N elements` … `subscription started` … `apply (as of first
   drain)`; visual troca.
4. Passe o mouse sobre um botão de app aberto: hover/press conforme o tema; ao
   sair, volta. Com `"logLevel":"debug"` no config, cada troca loga
   `apply <id> state '<Estado>'`.
5. `reset` — taskbar padrão; **`BackgroundStroke` reaparece** (prova de que o
   original foi restaurado).
6. `apply Pills` — log `merged 6 resource variables`; alterne claro/escuro no
   Windows: sem crash, cores acompanham.
7. Segundo monitor (se houver): a taskbar dele também estiliza; log mostra
   `initialized for thread` para a thread dela.
8. Abra e feche o menu Iniciar, a central de notificações e o flyout de
   configurações rápidas com um tema aplicado: sem crash; log mostra
   `drained N handles, M held` após cada rajada.
9. **Handles estáveis**: deixe a taskbar parada 10 minutos com tema aplicado.
   Registre o log durante o repouso e após `reset`. M em `drained … M held`
   conta apenas os handles mantidos **naquele lote**, calculados como
   `unique_count - to_release.size()` em `release_queue.cpp`; não representa
   todos os handles vivos. `held == elementos estilizados` não é um invariante
   que esse log possa verificar, e `0 held` após reset não prova ausência de
   vazamento global. Crescimento contínuo de memória ou liberações contínuas
   em repouso pedem investigação com tema e log. A contagem global exigida
   pela spec §7.2 continua pendente para o Plano 4: este item não deve ser
   marcado como prova de handles vivos estáveis com a instrumentação atual.

10. **Blur real.** `apply FrostyGlass` (ou `TranslucentTaskbar`). No log:
    `N blur brushes, 0 blur fallbacks`. Visual: a taskbar borra o papel de
    parede atrás dela e o borrão **acompanha** uma janela arrastada por baixo
    — o `AcrylicBrush` do Plano 3 também é translúcido, então a prova é o
    movimento, não a transparência.
11. **Tint por tema.** `apply Command_Center` (usa
    `TintColor="{ThemeResource SystemChromeMediumColor}"`). Alterne
    Configurações → Personalização → Cores entre claro e escuro: o tom da
    taskbar acompanha, sem reaplicar o tema.
12. **Ruído.** `apply Luminosity_variant_Classic`: o grão está presente, mas
    no `NoiseOpacity="0.1"` do tema é praticamente imperceptível (a amplitude
    escala com o quadrado do valor); só fica visível contra um papel de parede
    liso com `NoiseOpacity` bem mais alto.

13. **Variáveis de estilo.** `apply Pills`. No log: a linha `theme Pills: …`
    traz `N captures, M dynamic values` (não mais "captures skipped"), e o
    `apply (as of first drain)` termina em `K variables` com `K >= 1`.
    Visual: os botões de app viram pílulas e **cada** pílula tem a largura do
    próprio botão (prova da escolha do capturador mais próximo — se todas
    ficarem com a mesma largura, o escore está errado).
14. **Reação ao layout.** Com `Pills` aplicado, abra e feche aplicativos até a
    taskbar mudar de largura, e passe o mouse por um botão com rótulo. As
    pílulas reacompanham sem reaplicar o tema; em `Debug` o log mostra
    `dynamic ... unresolved for now` no máximo durante o primeiro relatório de
    cada botão, nunca em regime.
15. **Blob.** `apply Blob` — os paddings do
    `{{$buttonSpacing-2}},{{$taskbarTopOffset}},…` saem simétricos.

`propagation depth capped` no log significa um ciclo (uma propriedade
dinâmica que altera o que ela mesma captura) contido pelo cap: registre no
ledger qual variável.

`WindhawkBlur` não é aproximação: vira blur de composição real, e o
`AcrylicBrush` só entra como fallback contado (`blur fallbacks` no log).
Capturas `=>` e valores `{{…}}` também já são reais (Plano 3b/Task 6) — o log
de `theme …` diz quantos de cada.

### Exceção conhecida durante rebuild do painel de botões

Com `Pills`, mudar o agrupamento da taskbar ou abrir aplicativos pode fazer
`ActualWidth` passar por zero durante a reconstrução do painel. Nesse
intervalo, `{{BtnW-6}}` resolve para `-6`, e o XAML rejeita `MinWidth` ou
`MaxWidth` negativo com `0x80070057`.
O smoke da Task 6 registrou 136 `ERR` em 777 ms na expansão dos rótulos;
as pílulas se corrigiram após o layout, sem efeito visual persistente, e o
encolhimento não produziu erros. Esses números são evidência daquele smoke,
não um limite de aceitação.

Na validação da retomada, o nome da propriedade no log confirmou ambas:
28 erros de `MinWidth`/`MaxWidth` entre 19:52:12.226 e 19:52:12.730 (504 ms),
ao abrir três janelas com `Pills`, no Explorer PID 19132. As quatro linhas
correspondentes do tema usam `{{BtnW-6}}`; a captura
`final-pills-new-buttons.png` mostrou recuperação visual. Fechar as janelas
não produziu nova rajada. A observação total de dez minutos terminou às
20:00:44 com o mesmo PID e nenhuma linha de erro adicional; o reset às
20:01:22 restaurou 198 elementos. Isso não substitui a medição de handles
XAML vivos nem o ensaio adversarial do reinício histórico.

Esta é uma exceção restrita ao critério de zero `ERR`: registre separadamente
as linhas `apply ... property '<MinWidth ou MaxWidth>' ... failed 0x80070057`
associadas a `{{BtnW-6}}` durante o rebuild, a duração da rajada e a recuperação
visual.
Depois do layout, devem cessar os erros, as larguras devem acompanhar cada
botão e o PID do Explorer deve permanecer igual. Erros em regime ou com outra
causa continuam sendo falha do smoke. O contador `failed_styles` inclui a
rajada; não o interprete como zero falhas.

Não há guarda de valores negativos: a revisão final manteve a decisão de
registrar essa transição sem introduzir uma lista geral de propriedades que
rejeitam negativos (decisão 11 e retomada em
`docs/superpowers/plano-3b-decisoes.md`).

## Plano 4 — bandeja e ciclo de vida

Compilar o nativo, executar `tools/build-tray.ps1` e copiar o pacote inteiro
para uma pasta isolada com espaços no nome. Usar o executável dessa cópia.
Preservar `config.json` antes do teste e restaurar seus bytes ao terminar.

1. Iniciar com tema vazio: um único ícone, estado Inativo. Segunda instância
   deve abrir o diagnóstico existente e encerrar sem outro ícone, inclusive
   se a janela estiver minimizada. Menu mostra os temas selecionáveis e conserva
   o `&` dos nomes de apresentação.
2. Selecionar Command_Center, conferir aplicação real e estatísticas novas no
   log do PID atual. Trocar de tema sem reiniciar Explorer. Uma configuração
   `command_center` escrita pelo CLI também deve ser reconhecida e marcada.
3. Abrir/fechar Task View e outros hosts; desativar. Confirmar restauração nas
   threads alcançadas, assinatura encerrada, hook removido e timers parados.
   Abrir novos hosts já em reset não deve reativar estilos. Aplicar novamente.
4. Com tema ativo, usar Reiniciar o Explorer no menu. Confirmar PID novo,
   recuperação do ícone, carga e aplicação sem intervenção. Registrar PIDs,
   timestamps e identidade/hash da DLL efetivamente carregada.
5. Provocar falha controlada de carga num processo novo, por exemplo usando
   um pacote de teste cuja DLL esteja temporariamente indisponível. Não alterar
   DLL carregada nem instalar outro componente. Confirmar Falhou com HRESULT;
   após um poll de 30 s não há outra carga. Restaurar o arquivo e usar Tentar
   novamente. Somente essa ação ou TaskbarCreated permite nova tentativa.
6. Exportar árvore com tema ativo e novamente inativo: arquivo novo, não vazio,
   sem snapshot concorrente com a assinatura, com retomada do estado desejado.
   Ausência de arquivo novo é falha; não abrir silenciosamente o anterior.
   Caso negativo: após Task View fechado em reset, uma árvore pode denunciar
   filhos não entregues. Conferir erro, SHA/mtime anterior intactos, liberação
   completa e falha na bandeja após 15 s. Registrar esses erros esperados
   separadamente; uma exportação posterior válida deve recuperar normalmente.
7. Diagnóstico: última observação do PID + criação atuais, timestamp e idade.
   Registrar `observed`, `residual` e `incomplete` após apply, reset e export.
   Log ausente, parcial, filtrado ou antigo deve aparecer como indisponível ou
   desatualizado, nunca zero inventado. Residual/incompletude não são aprovados
   como prova de ausência de vazamento.
8. Desativar e sair: pedido de reset enviado, bandeja encerrada, visual original
   confirmado no shell e configuração original restaurada pelo roteiro.

Estes casos não validam monitor físico adicional nem estabilidade por 24 horas.
`Ativo` no ícone informa pedido aceito; a confirmação visual vem deste smoke e
das observações atuais, não de um canal de resposta que o projeto não possui.
