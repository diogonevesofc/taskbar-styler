# Plano 4 — decisões e execução

Base `8e8d52a`; branch `plano-4-app-bandeja`.
Plano: `plans/2026-09-14-plano-4-app-bandeja.md`.

## Decisões iniciais — 2026-09-14

1. Mantidos C#/.NET 10/WinForms, P/Invoke direto e config + sinais unidirecionais.
   O SDK 10.0.401 e WindowsDesktop 10.0.12 estão instalados; nenhuma instalação
   global é necessária. Não há autostart ou telemetria nesta etapa.
2. Reset atual não era inerte: reabria assinatura mesmo com tema vazio e não
   parava hook/timer. Também há dependência circular UI/wait, parada Deferred
   invisível para uma segunda tentativa e falha de Unadvise tratada como sucesso.
   Essas correções pertencem ao ciclo de vida exigido pela bandeja.
3. `held` continua insuficiente. A nova métrica conta registros observados sem
   liberação confirmada, incluindo retenção entre lotes e falhas. Não promete
   consultar o total privado do XAML nem somar wrappers como caches independentes.
4. Exportação nova precisa de comando próprio. Um segundo Event Tray→TAP amplia
   §4.2 sem acrescentar resposta, protocolo versionado ou carga repetida da DLL.
   A exportação pausa/reinicia a aplicação na mesma sessão; esse custo explícito
   de diagnóstico permite reaproveitar o snapshot com revisão de concorrência.
5. `Ativo` informa pedido aceito, não confirmação visual. A janela de diagnóstico
   separa configuração de observações do log e não transforma ausência de dados
   em saúde confirmada.

Implementação, revisão independente e smoke concluídos em 2026-09-14.

## Integração e revisão

- Ledger puro: nove testes/75 asserções passaram em build isolado `/W4`.
- Núcleo C#: 33 testes passaram, sem pacotes externos. Catálogo medido:
  55 arquivos e 54 selecionáveis. `Oversimplified_Accentuated.json` tem ID
  interno `Oversimplified&Accentuated`; a identidade de carga deve ser o nome
  do arquivo, como no TAP. Comparações com config seguem a caixa indiferente
  do Windows, inclusive para configurações gravadas pelo CLI.
- Revisão da carga/reinício identificou reutilização de PID entre `StartTime`
  e ação: manter `Process.SafeHandle` aberto antes da verificação e durante a
  operação. Inspeção do runtime instalado confirmou que `StartTime` sozinho
  usa apenas um handle temporário.
- Exportação agora também pertence à geração da máquina de estados: falha
  no processo novo não pode virar retry automático no poll seguinte. Tooltip
  coloca HRESULT antes do motivo para não perdê-lo no limite de caracteres.
- Primeiro uso do SDK informou e criou incidentalmente um certificado ASP.NET
  em CurrentUser/My, em 2026-09-14 21:47:06, thumbprint
  `F8EE614D31E14645FE09155F2E24F387AF445434`. Não consta nas raízes de confiança
  de CurrentUser ou LocalMachine; não foi executado `--trust`. O certificado
  anterior foi preservado. Chamadas posteriores e o script de build desabilitam
  geração de certificado e telemetria por variáveis do processo.
- `actions/setup-dotnet@v6` foi conferido no
  [repositório oficial](https://github.com/actions/setup-dotnet); SDK da linha
  10.0.x no CI, `global.json` permite feature bands da mesma versão 10.0.
- Segunda execução abre o diagnóstico da instância existente. O diagnóstico
  oferece `Abrir menu`, restaura estado minimizado e explicita abertura mesmo
  quando o launcher usou `SW_HIDE`. O primeiro smoke confirmou uma só instância,
  diagnóstico e menu visíveis no segundo display enumerado; a captura inicial
  do display principal não continha essa janela.
- Revisão final da UI: completar exportação somente após iniciar o visualizador;
  recusar publicação em saída com JSONs de temas ausentes do catálogo atual.
  Teste com `obsolete.json` confirmou recusa antes de publicar e preservou o
  arquivo. Build WinForms Release com zero warnings/erros; 33 testes C#,
  22 Python e round-trip de 55 temas passaram novamente.

## Ajustes encontrados no smoke

- A exportação produziu arquivo novo, mas `System32/notepad.exe` não existe
  neste Windows. O app agora abre o caminho `.txt` pela associação do usuário;
  a repetição abriu a árvore no Notepad++ instalado, em estado ativo e inativo.
- O primeiro reinício por menu (`19360` → `13888`) recuperou o tema, mas uma
  primeira varredura terminou em `0x80070490` antes de a conexão XAML estar
  pronta. A carga passa a esperar essa prontidão por até 5 s, somente para
  `ERROR_NOT_FOUND`, dentro da operação. §6.5 registra essa exceção; falhas
  concluídas continuam bloqueadas no poll. O prazo não cancela COM bloqueado.
- Task View entregou dois relatórios antes da inicialização da thread; a
  métrica marcou corretamente a perda de cobertura. A fila agora conserva
  esses relatórios na UI de origem, sem ligar a estilização antecipadamente.
- O leitor compartilhado do diagnóstico expôs incompatibilidade de sharing
  no logger nativo: `_wfopen_s` recusava abrir enquanto o leitor permanecia
  aberto. Um teste UCRT reproduziu `errno=13`; `_wfsopen` com `_SH_DENYNO`
  corrigiu o caso e a regressão com leitor simultâneo passou. O CTest isola
  `LOCALAPPDATA` do processo de teste dentro do build.
- Uma árvore parcial após Task View era detectada no log, mas ainda substituía
  o arquivo válido. Agora retorna `E_FAIL` antes de qualquer escrita. A limpeza
  e a retomada continuam; a bandeja falha após 15 s sem arquivo novo.
- O reinício pelo menu não antecipa mais um `TaskbarCreated` sintético quando
  apenas o HWND do shell aparece. O evento real e o poll permanecem ativos.
  Neste Windows, a prontidão levou cerca de 32 s; o ensaio final recuperou o
  tema sem intervenção e sem erro de carga.

## Artefatos e testes finais

- DLL final: `93DA4A2EFD9B493BC3631601D42228E45FB36072393AF43C00C8EF5D344101D2`.
- CMake/CTest: core **129 testes / 7808 asserções**, TAP **55 / 282**; todos
  passaram. Apenas warnings pré-existentes C4002/C5285. WinForms Release sem
  warnings novos; núcleo C# **33/33**; Python **22/22**; 55 temas reconstruídos
  byte a byte. Publicação x64 dependente do runtime concluída e pacote conferido.
- Execução em cópias fora do repositório, com espaços no caminho. DLL carregada
  foi identificada por `Process.Modules`, caminho e SHA; nenhum fallback ao
  checkout. Pacote distribuível: `out/tray`.
- CI atualizado, sem execução remota nem push nesta entrega.

## Smoke final e evidências

Horários locais UTC−03. Evidências brutas em
`.superpowers/sdd/2026-09-14-plano-4-app-bandeja/`, ignorado pelo git.

| Cenário | Resultado observado |
|---|---|
| Menu e instância única | Menu pelo diagnóstico: 54 temas selecionáveis; `&` preservado; segunda execução abre a janela existente, inclusive minimizada; saída 0 e um só processo |
| Configuração do CLI | `command_center` reconhecido como `Command_Center`, item marcado `On`; campo e caixa preservados |
| Aplicar/trocar/reset | Command_Center e TranslucentTaskbar aplicados sem novo PID; visual restaurado, assinatura/hook/timers encerrados |
| Callbacks antecipados | PID 25268, TID 2808: duas notificações antes de inicializar foram enfileiradas; 36+15 elementos restaurados e ledger final `0/0/0` |
| Exportação inicial | Snapshot 315 → arquivo → liberação confirmada → tema → assinatura; log completo após correção de sharing |
| DLL ausente | PID 25268: uma falha `0x80070002`, nenhuma carga após 65 s; DLL disponibilizada às 22:34:50 e ainda não carregada às 22:35:43; somente retry às 22:35:45 carregou |
| Exportação parcial rejeitada | PID 22272, às 22:42:24: dois ItemsPresenter com 2/3 filhos; `E_FAIL`; SHA e timestamp anteriores intactos; liberação concluída e UI Falhou |
| Recuperação da exportação | Mesmo PID 22272: snapshot 308 às 22:43:33, arquivo novo, ledger zerado antes da retomada; nenhum erro adicional |
| Reinício final pelo menu | PID 16356 → 5024; pedido 22:45:40.663, evento 22:46:12.160, carga aceita 22:46:12.300; sem intervenção e sem falha de carga |
| Exportação inativa final | PID 5024: snapshot 308 às 22:47:11.203, arquivo novo, ledger `observed=0 incomplete=0 residual=0`, TAP inativo |
| Encerramento | Reset e saída às 22:47:14; visual original confirmado; configuração original restaurada byte a byte |

O caso parcial preservou SHA
`25CC4C42059A9166EF10E2D9650D09679CB0CB317FA0D6DF6930D2B0ACF5A74A`
e mtime `2026-09-15T01:41:26.1832513Z`. Os três `ERR` desse caso negativo
(duas contagens incompletas e falha da exportação) são esperados. O último
processo, PID 5024, não registrou `ERR`; a consulta ao Application log desde
22:20 não encontrou eventos 1000/1001/1002/1026 de Explorer/TaskbarStyler.

Configuração restaurada: SHA
`FFA61D589CCBCD4836DA527FD21049CBA8D071A70384AACA67ACA776F47255BF`.
Nenhuma alteração de autostart, pré-requisito XAML ou configuração de dumps
foi necessária para este plano. O efeito incidental do SDK está registrado acima.

Conferência adicional do ícone: `Shell_NotifyIconGetRect` reconheceu HWND/ID
do processo e retornou `S_OK`. O clique nas coordenadas devolvidas abriu o menu
do Codex, portanto **clique direto no ícone não foi validado neste desktop**.
Não se alterou a configuração da área de notificação. O menu e todos os comandos
foram exercitados pelo diagnóstico. Após essa conferência, a última instância
foi encerrada e a configuração restaurada novamente às 22:53:30. Às 22:56,
PID 5024 permanecia igual, sem `ERR` ou evento de crash, com ledger `0/0/0`.

Commits por task: ledger `4b56fd8`; ciclo de vida `0f9450e`; núcleo C#
`a64f48e`; interface/adaptador `ce4f257`. A persistência foi commitada antes
da integração nativa por ser independente. Empacotamento `6827a5c` e registro
final completam o branch antes do fast-forward local.

## Limites preservados

- A métrica cobre registros observados pelo TAP, não todo o cache interno do
  XAML; zero não demonstra ausência universal de vazamentos.
- Uma captura parcial pode ocorrer em hosts transitórios: o arquivo válido é
  preservado e a UI indica falha; não se inventam filhos nem se reduz o contador.
- Não foram concluídos ensaio de 24 h nem matriz controlada de monitores/DPI/
  hot-plug. Displays enumerados e capturas não substituem essa matriz.
- Não há cancelamento seguro de uma chamada privada XAML bloqueada. O prazo
  de prontidão limita novas chamadas, não a duração de COM já em execução.
