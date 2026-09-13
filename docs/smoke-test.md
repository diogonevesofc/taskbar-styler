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
