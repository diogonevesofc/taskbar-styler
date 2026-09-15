# Licenças e avisos de terceiros

O Taskbar Styler combinado é distribuído sob **GPL-3.0-only**. O texto está em
[LICENSE](LICENSE); a atribuição e as modificações estão em [NOTICE](NOTICE).
As dependências abaixo conservam suas próprias licenças. Seus avisos completos
acompanham o pacote na pasta `licenses/`.

## Código e temas

| Componente | Versão ou referência | Licença e aviso |
|---|---|---|
| Windows 11 Taskbar Styler, de m417z | Mod 1.9, cópia em `vendor/upstream/` | GPL-3.0-only; [proveniência](vendor/upstream/PROVENANCE) e [LICENSE](LICENSE) |
| nlohmann/json, de Niels Lohmann | 3.11.3, commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` | MIT; [texto original](licenses/nlohmann-json-LICENSE.MIT) |
| doctest, de Viktor Kirilov | 2.4.11, commit `ae7a13539fb71f270b87eb2e874fbac80bc8dda2` | MIT; [texto original](licenses/doctest-LICENSE.txt); usado somente nos testes |
| C++/WinRT, Microsoft | Headers do Windows SDK 10.0.26100.0; versão 2.0.250303.5 | MIT; [texto original](licenses/cppwinrt-LICENSE.txt) |

Os JSONs foram extraídos das tabelas do mod vendorizado. [THEMES.md](THEMES.md)
preserva os créditos individuais obtidos no guia de estilos. Essa referência
não declara uma licença geral para o guia ou suas imagens. A prévia do app é
uma ilustração própria derivada dos JSONs.

Origem dos textos preservados:

- [nlohmann/json — LICENSE.MIT](https://github.com/nlohmann/json/blob/9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03/LICENSE.MIT).
  O arquivo de licença contém copyright 2013–2022; o header 3.11.3 contém
  copyright 2013–2023 Niels Lohmann. Ambos os avisos originais são preservados.
- [doctest — LICENSE.txt](https://github.com/doctest/doctest/blob/ae7a13539fb71f270b87eb2e874fbac80bc8dda2/LICENSE.txt).
- [C++/WinRT — LICENSE](https://github.com/microsoft/cppwinrt/blob/76ab8890c1cce78a9c68d3a99a5eb8129be9a3f0/LICENSE).
  Referência fixa do texto MIT; os headers usados no build também declaram
  copyright Microsoft e licença MIT.

## Runtime incluído na distribuição Windows x64

A publicação self-contained inclui **.NET 10.0.12**. Os pacotes NuGet
`Microsoft.NETCore.App.Runtime.win-x64` e
`Microsoft.WindowsDesktop.App.Runtime.win-x64` dessa versão declaram licença
MIT, com repositório `dotnet/dotnet` e commit
`95017c711e6afc1085133d440e42b4bd78155701` nos seus manifests.

O script de publicação copia dos pacotes exatos para `licenses/`:

- `microsoft.netcore.app.runtime.win-x64-10.0.12-LICENSE.TXT`;
- `microsoft.netcore.app.runtime.win-x64-10.0.12-THIRD-PARTY-NOTICES.TXT`;
- `microsoft.windowsdesktop.app.runtime.win-x64-10.0.12-LICENSE`.

Esses arquivos vêm da raiz de cada runtime pack no cache NuGet, não do
`LICENSE.txt` da instalação do SDK. O pacote WindowsDesktop não contém um
arquivo de avisos de terceiros. Por isso os seguintes avisos são preservados
a partir dos repositórios oficiais da mesma versão:

| Componente | Revisão da tag `v10.0.12` | Avisos incluídos |
|---|---|---|
| Windows Forms | `c276f7ec4b4684015410a417bb590deb7825ce5c` | [arquivo local](licenses/dotnet-winforms-10.0.12-THIRD-PARTY-NOTICES.TXT), [fonte](https://github.com/dotnet/winforms/blob/c276f7ec4b4684015410a417bb590deb7825ce5c/THIRD-PARTY-NOTICES.TXT) |
| WPF | `3bbc551873ff9afc718eec20077a9118c15b0688` | [arquivo local](licenses/dotnet-wpf-10.0.12-THIRD-PARTY-NOTICES.TXT), [fonte](https://github.com/dotnet/wpf/blob/3bbc551873ff9afc718eec20077a9118c15b0688/THIRD-PARTY-NOTICES.TXT) |

O pacote Desktop inclui componentes WPF mesmo quando o app usa Windows Forms.
Os avisos abrangem os componentes distribuídos pelo pacote. Ao atualizar o
runtime, atualize também esses avisos e as revisões documentadas.

## Fonte correspondente

Cada release deve apontar, junto ao instalador, para a fonte da mesma versão:
código, temas, referência upstream e scripts de build e instalação. As
dependências de código aberto devem permanecer identificadas por suas revisões
fixas, com acesso às fontes correspondentes. Os avisos de licença e atribuição
acompanham tanto a fonte quanto os binários.

A versão pública usa C++ em Release com CRT estático. DLLs de Debug do Visual
C++ não fazem parte da distribuição. Os componentes do Windows continuam
fornecidos pelo próprio sistema operacional.
