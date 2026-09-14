# taskbar-styler

Customiza a taskbar do Windows 11 sem depender do Windhawk.

Obra derivada do mod [windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods)
(m417z, GPL-3.0), reescrita como aplicativo independente.

## Como funciona

O Windows carrega uma DLL COM dentro do `explorer.exe` por meio da API de
diagnóstico do XAML (`InitializeXamlDiagnosticsEx`) — o mesmo mecanismo do Live
Visual Tree do Visual Studio. Essa DLL recebe a árvore visual da taskbar e
aplica os estilos do tema escolhido.

**Não há injeção de DLL nem patch de código.** Nada de `CreateRemoteThread`,
`WriteProcessMemory` ou hooks inline: apenas APIs sancionadas do Windows. E não
há nenhum acesso à rede em runtime.

## Estado

Em desenvolvimento. O que já existe:

- [x] `styler_core` — parsing de seletores, regras de estilo e temas
- [x] 55 temas em JSON, com conversão provada sem perda byte a byte
- [x] TAP que carrega no explorer e exporta a árvore visual
- [x] Aplicar e desfazer estilos (Plano 3)
- [ ] O aplicativo de bandeja (Plano 4)

## Compilando

Requer Visual Studio 2026 com o toolchain C++ e o Windows SDK.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Vendo a árvore visual

```
taskbar-styler load
```

Escreve `%LOCALAPPDATA%\TaskbarStyler\visual-tree.txt` com a árvore da sua
taskbar, no mesmo formato dos seletores dos temas. É com isso que você conserta
um tema sozinho quando uma atualização do Windows renomeia algum elemento.

## Uso

```
taskbar-styler load               carrega o TAP no explorer.exe
taskbar-styler apply <ThemeId>    aplica um tema (ao vivo, se o TAP ja estiver carregado)
taskbar-styler reset              desfaz o tema aplicado (ao vivo)
taskbar-styler list               lista os temas disponiveis
taskbar-styler status             mostra o estado (TAP carregado, tema configurado, composition diagnostics)
taskbar-styler setup              grava DisableCompositionDiag=1 (precisa de administrador)
```

`apply` e `reset` escrevem `%APPDATA%\TaskbarStyler\config.json` e sinalizam
um Event nomeado; se o TAP já estiver carregado no `explorer.exe`, o tema
troca ao vivo, sem passar por `load` de novo. **Trocar de tema não reinicia o
Explorer; descarregar o TAP, sim (spec §6.4).**

`WindhawkBlur` vira blur de composição real (grafo de efeitos D2D sobre o
backdrop da janela, com `BlurAmount`, saturação, luminosidade e ruído); o
`AcrylicBrush` é o fallback quando a composição não está disponível — o log de
`apply …` conta `blur brushes` e `blur fallbacks`.

O que ainda é aproximação (Plano 3b): capturas `=>` e valores `{{…}}` são
pulados — o log de `theme …` diz quantos.

## Temas

Ficam em [`themes/`](themes/), um arquivo JSON por tema, editáveis sem
recompilar. Os créditos estão em [THEMES.md](THEMES.md).

## Licença

GPL-3.0. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).
