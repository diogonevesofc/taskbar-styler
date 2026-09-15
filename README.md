# Taskbar Styler

**Personalize a barra de tarefas do Windows 11. Grátis, com código aberto e sem Windhawk.**

[Baixar a beta para Windows x64](https://github.com/diogonevesofc/taskbar-styler/releases/tag/v0.1.0-beta.1) ·
[Ver todos os temas](THEMES.md) ·
[Relatar um problema](https://github.com/diogonevesofc/taskbar-styler/issues/new/choose)

![Catálogo de temas e prévia ilustrativa](docs/images/theme-browser.jpg)

## O que você pode fazer

- Escolher entre **54 temas** e buscar por nome ou autor.
- Ver uma **prévia ilustrativa antes de aplicar**, com fundo claro ou escuro.
- Trocar o tema sem reiniciar o Explorer e restaurar o visual padrão.
- Manter o app na bandeja e recuperar a personalização quando o Explorer reiniciar.
- Consultar diagnóstico e logs quando uma atualização do Windows afetar um tema.

Sem conta, cobrança, anúncios, telemetria ou conexão de rede durante o uso.
Não configura inicialização automática e não instala o Windhawk.

## Instalação

1. Abra a [página da versão](https://github.com/diogonevesofc/taskbar-styler/releases/tag/v0.1.0-beta.1).
2. Baixe **TaskbarStyler-0.1.0-beta.1-win-x64-setup.exe**.
3. Execute o instalador, confira a licença e conclua a instalação.
4. Abra **Taskbar Styler** pelo menu Iniciar.

**Requisitos:** Windows 11 **x64** e permissão de administrador para instalar.
ARM64, Windows 10 e Windows Server não são suportados nesta versão.
O instalador inclui o runtime .NET; não é necessário instalar .NET ou Visual
Studio separadamente. O uso normal do aplicativo não exige administrador.

O instalador solicita consentimento para habilitar o pré-requisito de diagnóstico
XAML do Windows quando ele está ausente. A configuração compartilhada existente
é preservada; um valor incompatível bloqueia a instalação com uma explicação.

Esta beta ainda **não tem assinatura digital**. O Windows pode mostrar um aviso
de editor desconhecido. Use somente os arquivos desta página de Releases;
o arquivo SHA256SUMS.txt permite conferir a integridade do download.

### Atualizar ou desinstalar

Antes de atualizar, use **Diagnóstico → Ferramentas → Desativar e sair**.
Instale a nova versão. Se uma DLL da versão anterior ainda estiver carregada,
use **Reiniciar o Explorer** nas ferramentas do aplicativo ou reinicie o Windows.
O instalador não força o encerramento do Explorer.

Para remover, desative o tema, encerre o app e use **Configurações do Windows →
Aplicativos → Aplicativos instalados → Taskbar Styler → Desinstalar**.
Arquivos em uso podem ser removidos no próximo reinício. Suas configurações e
logs são preservados. O pré-requisito compartilhado do Windows também permanece.

## Como usar

1. Busque e selecione um tema. **Selecionar não altera a taskbar.**
2. Compare a prévia. **Claro/Escuro** muda somente o fundo da ilustração.
3. Clique em **Aplicar tema**. Para desfazer, use **Restaurar padrão**.

Fechar a janela mantém o programa na bandeja. Clique no ícone ou execute o
programa novamente para abrir o catálogo da mesma instância. Ctrl+F foca a
busca; Esc fecha a janela para a bandeja.

A prévia simplifica layout, ícones e efeitos. **Não é uma captura nem uma
reprodução exata da sua barra.** O resultado depende da versão do Windows,
do papel de parede, dos aplicativos fixados e dos seletores do tema.

## Estado da beta e limitações

**0.1.0-beta.1 é uma versão de testes para uso voluntário.** O app atua dentro
do Explorer; uma incompatibilidade pode interromper ou reiniciar o shell.

- Desenvolvimento e testes ao vivo em Windows 11 25H2 x64, build 26200.9445.
- Catálogo, prévia, aplicação, reset e recuperação do Explorer foram exercitados.
- A matriz completa de monitores/DPI, conectar/desconectar monitores e a
  observação de 24 horas continuam pendentes.
- Atualizações do Windows podem mudar os elementos da barra e afetar temas.
- O estado **Ativo** indica que o pedido foi aceito; não confirma cada regra.

Resultados e limites detalhados estão em [docs/STATUS.md](docs/STATUS.md).

## Resolver problemas

| Situação | Ação |
|---|---|
| O tema não mudou | Abra Diagnóstico, confira o erro e use Ferramentas → Tentar novamente. |
| A prévia difere da barra | A prévia é ilustrativa. Confira também sua versão do Windows. |
| “TAP de outra pasta carregado” | Desative o tema e use Ferramentas → Reiniciar o Explorer. |
| Pré-requisito ausente | Execute novamente o instalador com permissão de administrador. |
| O ícone está oculto | Execute o programa novamente; ele abre o catálogo existente. |
| Quer voltar ao padrão | Use Restaurar padrão. Se o shell não responder, reinicie o Explorer ou o Windows. |

Configuração: `%APPDATA%\TaskbarStyler\config.json`.
Logs: `%LOCALAPPDATA%\TaskbarStyler\log.txt` e `tray.log`.
O diagnóstico também permite exportar a árvore visual para investigar um tema.
Ao abrir uma issue, revise os logs e compartilhe apenas o necessário.

## Código-fonte e compilação

O código desta versão está na [tag v0.1.0-beta.1](https://github.com/diogonevesofc/taskbar-styler/tree/v0.1.0-beta.1).
O arquivo de fontes da release corresponde ao commit usado para o instalador.

Requisitos de desenvolvimento: Visual Studio com **Desenvolvimento para desktop
com C++**, Windows SDK 10.0.26100.0 ou compatível, CMake 3.25+, Ninja, .NET SDK 10
e Python 3.12+. Use um Developer PowerShell/Prompt **x64**.

```powershell
git clone https://github.com/diogonevesofc/taskbar-styler.git
cd taskbar-styler
git checkout v0.1.0-beta.1

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build-release
ctest --test-dir build-release --output-on-failure

$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
dotnet run --project tests/tray/TaskbarStyler.Tray.Tests.csproj -c Release
./tools/build-tray.ps1 -NativeDirectory build-release/src/cli -OutputDirectory out/release/app -SelfContained
```

Para gerar o instalador, tenha o **Inno Setup 6.7.1** e execute:

```powershell
./tools/build-installer.ps1 -PackageDirectory out/release/app -OutputDirectory out/release
```

O [workflow de release](.github/workflows/release.yml) registra o processo de
build e verificação. Dependências são obtidas durante o build; o aplicativo
distribuído funciona offline. Não distribua o TAP de um build Debug.

## Contribuir

Issues e contribuições são bem-vindas. Veja [CONTRIBUTING.md](CONTRIBUTING.md)
para reproduzir problemas, executar testes e propor mudanças pequenas.

## Licença e créditos

Distribuição gratuita sob **GNU GPL v3.0**. Você pode usar, estudar, modificar e
redistribuir conforme a [licença](LICENSE), mantendo suas condições.

Obra derivada de [Windows 11 Taskbar Styler](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/windows-11-taskbar-styler.wh.cpp),
de **m417z**, com temas contribuídos pela comunidade. Os JSONs foram extraídos
das tabelas desse mod. Créditos: [THEMES.md](THEMES.md) e [NOTICE](NOTICE).

As dependências mantêm suas próprias licenças; veja
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Este é um projeto independente,
sem afiliação com a Microsoft ou o Windhawk.
