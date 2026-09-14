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
   `initial apply: … 0 failed`.
3. `apply Lucent` (sem reiniciar o Explorer) — log `reload requested` …
   `reload applied`; visual troca.
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
   A última linha `drained … M held` não deve mudar de M, e `released so far`
   não deve crescer. Um M que cresce com a taskbar parada é vazamento — reporte
   com o tema e o log.

O que ainda é aproximação (Plano 3b): `WindhawkBlur` vira `AcrylicBrush`
(sem ruído e sem `BlurAmount`); capturas `=>` e valores `{{…}}` são pulados —
o log de `theme …` diz quantos.
