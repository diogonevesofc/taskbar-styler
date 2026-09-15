# Plano 6 — distribuição pública

## Entrega e autorização

O dono solicitou explicitamente distribuição gratuita, instalador público,
código-fonte no GitHub e README. Push e publicação fazem parte desse pedido.
Repositório: https://github.com/diogonevesofc/taskbar-styler.

Primeira versão: `0.1.0-beta.1`, Windows 11 x64. A classificação beta preserva
os limites registrados nos Planos 4/5: matriz de monitores/DPI/hot-plug e
observação de 24 h não concluídas. Não houve alteração do motor nesta entrega.

## Pacote

- CLI/TAP: CMake Release, CRT estático (`MultiThreaded`), sem DLLs de Debug.
- Bandeja: self-contained win-x64, .NET 10.0.12 fixado; uso offline.
- Inno Setup: instalação administrativa em Program Files; payload por versão,
  atalho de Iniciar e desinstalador. Nenhum início automático ou encerramento
  forçado do Explorer. A bandeja aberta bloqueia instalação/desinstalação.
- Pré-requisito XAML: DWORD 1 existente preservado; criação exige consentimento;
  valor existente incompatível bloqueia instalação. Configurações, logs e
  pré-requisito compartilhado permanecem após desinstalar.
- Atualização: DLL antiga residente exige reinício voluntário do Explorer ou
  Windows; a aplicação detecta e explica a divergência de pasta.
- Sem certificado Authenticode nesta beta. O README informa isso e disponibiliza
  verificação de integridade, sem instruir a contornar proteções do Windows.

## Fonte e licenças

A obra combinada é GPL-3.0-only, conforme o aviso explícito do mod upstream v1.9.
Headers próprios GPL-3.0-or-later e o texto GPL existente foram preservados.
Os JSONs vêm das tabelas desse mod; o guia de estilos é referência de créditos,
sem atribuir uma licença geral ao guia ou às imagens.

O pacote leva os avisos MIT de JSON/C++/WinRT e as licenças dos runtime packs
exatos do NuGet. Avisos WinForms/WPF vêm de revisões imutáveis da mesma versão.
O workflow gera fontes com `git archive HEAD`, inclui `COMMIT.txt` no pacote e
calcula SHA-256 do instalador, ZIP portátil e arquivo de fontes.

## Verificação

Local: CTest core/TAP, 58 testes C#, 22 testes Python e round-trip dos 55 temas
passaram. Auditoria dos imports confirmou ausência de CRT global/Debug.
Revisão independente do histórico e diff não encontrou credenciais ou arquivos
privados novos. Scratch, pacotes, símbolos e dumps ficam fora do Git.

O primeiro CI revelou um teste com caminho relativo fixo, sensível à pasta
extra de `Platform=x64`; a correção reproduziu 57/58 antes e 58/58 depois,
com e sem essa variável. Os logs nativos remotos têm apenas os warnings
conhecidos do SDK/doctest. Nenhum warning novo do projeto.

O ISCC 6.7.1 tem recursos `FileVersion`/`ProductVersion` iguais a 0.0.0.0.
A versão mínima é conferida pelo banner emitido na compilação real. Isso
evita rejeitar o compilador compatível instalado no runner.

O compilador Inno portátil não foi executado localmente: a revisão automática
bloqueou essa ação. O build usa o compilador já presente no runner. O instalador
público rejeita Windows Server; uma variante exclusiva de teste permite testar
arquivos/registro/desinstalação nesse runner, sem executar a aplicação XAML.
Essa variante e os logs de teste não entram nos assets públicos.

## Resultado final

Release pública: [v0.1.0-beta.1](https://github.com/diogonevesofc/taskbar-styler/releases/tag/v0.1.0-beta.1).
Fonte/binários: commit `99ee3854cac3bac4a7dfd6aa7d356479067b84c1`.

- [CI geral 35016178011](https://github.com/diogonevesofc/taskbar-styler/actions/runs/35016178011): aprovado.
- [Release 35016177411](https://github.com/diogonevesofc/taskbar-styler/actions/runs/35016177411): aprovado, compilador Inno 6.7.1.
- Rejeição de Server no instalador público; conflito e ausência de consentimento
  bloqueados; instalação e desinstalação da variante de teste concluídas sem
  reinício. Payload instalado idêntico por SHA-256, configurações/logs e chave
  compartilhada preservados, aplicativo não iniciado pelo instalador.
- Downloads conferidos por SHA-256. Auditoria independente: source.zip com os
  230 arquivos da revisão e comentário do commit; portátil com 343 arquivos,
  licenças e runtime 10.0.12, sem símbolos, dados pessoais ou variante de teste.

Instalador: **37.765.530 bytes**, SHA-256
`b8271d12d145b74389ee87085997c2efc4208a2747b7b51aee03f5346e14cda8`.

No Windows 11 25H2 build 26200.9445, o ZIP do CI foi extraído em pasta com espaços.
`hostfxr`, `hostpolicy` e `coreclr` foram carregados dessa pasta; o Explorer
carregou o TAP do mesmo pacote, SHA-256
`f85a788febe754e4ae173795c388b687d5807c40e38340c1b9b1152476cf6201`.

O Explorer foi reiniciado uma vez para trocar a DLL antiga (`20712 → 15848`).
Depois, o PID permaneceu durante carga do tema original, busca DockLike, prévia
clara/escura, aplicação e reset. A busca/prévia manteve os bytes de configuração.
DockLike registrou 28 elementos/48 propriedades e zero falhas; reset registrou
`observed=0 incomplete=0 residual=0`. Não interpretar esse snapshot como prova
de ausência de vazamentos durante 24 h.

Tema original `OS26_Liquid_Glass_variant_DarkTaskbar` e configuração restaurados
byte a byte; zero `ERR` nativos nesse Explorer. Esc escondeu a janela, e executar
novamente reabriu a mesma bandeja PID `364`. O app ficou aberto. Evidências locais
estão no scratch ignorado `.superpowers/sdd/2026-09-15-plano-6-distribuicao/`.

Não foi feita instalação administrativa local em Windows 11 limpo; o ensaio
local usou os mesmos arquivos do payload. Não houve alteração de configurações
globais nem instalação de ferramenta na máquina de desenvolvimento. O registro
final de documentação em `main` é posterior à tag; não modifica os artefatos.
