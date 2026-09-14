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
   O invariante é `held == elementos estilizados` — M em `drained … M held`
   é a contagem de handles retidos porque o elemento carrega estado nosso,
   não uma contagem solta; confira que M bate com o `elements` da última
   linha `apply (as of first drain)`, não só que M "não muda" de um drain
   para o outro. Depois de um `reset` (nenhum tema aplicado), M deve ser 0.
   `released so far` não deve crescer com a taskbar parada. Um M que cresce,
   ou que não bate com o total de elementos estilizados, é vazamento —
   reporte com o tema e o log.

10. **Blur real.** `apply FrostyGlass` (ou `TranslucentTaskbar`). No log:
    `N blur brushes, 0 blur fallbacks`. Visual: a taskbar borra o papel de
    parede atrás dela e o borrão **acompanha** uma janela arrastada por baixo
    — o `AcrylicBrush` do Plano 3 também é translúcido, então a prova é o
    movimento, não a transparência.
11. **Tint por tema.** `apply Command_Center` (usa
    `TintColor="{ThemeResource SystemChromeMediumColor}"`). Alterne
    Configurações → Personalização → Cores entre claro e escuro: o tom da
    taskbar acompanha, sem reaplicar o tema.
12. **Ruído.** `apply Luminosity_variant_Classic`: o grão fino é visível
    contra um papel de parede liso.

O que ainda é aproximação (Plano 3b): capturas `=>` e valores `{{…}}` são
pulados — o log de `theme …` diz quantos. `WindhawkBlur` já não é
aproximação: vira blur de composição real, e o `AcrylicBrush` só entra como
fallback contado (`blur fallbacks` no log).
