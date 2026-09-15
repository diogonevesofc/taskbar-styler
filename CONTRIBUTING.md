# Como contribuir

Issues e pull requests são bem-vindos. O projeto é gratuito e distribuído sob
GPL v3; preserve os avisos de autoria e as licenças das partes que modificar.

## Relatar um problema

Use [o formulário de bug](https://github.com/diogonevesofc/taskbar-styler/issues/new?template=bug_report.yml)
e inclua versão do aplicativo, versão/build do Windows (`winver`), tema e
passos para reproduzir. Para problemas visuais, informe monitores e escalas
de DPI, resultado esperado e uma captura relevante.

Logs ficam em `%LOCALAPPDATA%\TaskbarStyler\log.txt`. Revise o conteúdo antes
de publicar: remova caminhos pessoais e outras informações privadas. Prefira
o trecho correspondente ao problema; não envie dumps completos ou credenciais.

## Alterar o código

1. Faça um fork e crie uma branch a partir de `main`.
2. Leia [AGENTS.md](AGENTS.md) e [o estado do projeto](docs/STATUS.md).
3. Mantenha a alteração focada e descreva o problema, a mudança e as evidências
   no pull request. Código e comentários em inglês; documentação em português.
4. Execute as verificações aplicáveis abaixo. Mudanças no TAP exigem também
   [smoke no Windows 11](docs/smoke-test.md); registre o que não foi testado.

## Verificações

Com os pré-requisitos de build descritos no [README](README.md#código-fonte-e-compilação):

```powershell
python -m pip install pytest
python -m pytest tools/ -q
python tools/extract_themes.py roundtrip --source vendor/upstream/windows-11-taskbar-styler.wh.cpp --expect-count 55
ctest --test-dir build-release --output-on-failure
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
dotnet run --project tests/tray/TaskbarStyler.Tray.Tests.csproj -c Release
```

O catálogo deve continuar reproduzível a partir da fonte vendorizada e manter
os créditos. O aplicativo permanece sem acesso à rede em runtime. As
restrições de interoperabilidade e de callbacks do Explorer estão em AGENTS.md.
