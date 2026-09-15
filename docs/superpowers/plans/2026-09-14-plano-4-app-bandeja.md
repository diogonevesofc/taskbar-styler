# Plano 4 — app de bandeja e ciclo de vida

Base: `main` em `8e8d52a`. Branch: `plano-4-app-bandeja`.
Autoridade: `../specs/2026-09-12-taskbar-styler-design.md`.

Implementação e revisão concluídas. Execução, correções de runtime e limites
do smoke registrados em `../plano-4-decisoes.md`. A validação dos comandos foi
feita pelo diagnóstico; o clique direto no ícone permanece pendente.

## Resultado esperado

Aplicativo WinForms/.NET 10 x64, sem janela principal, com seleção dos temas
existentes, desativação, diagnóstico, exportação da árvore e recuperação após
reinício do Explorer. Sem instalação, autostart, telemetria ou retry periódico
de uma carga que falhou. O reset deixa o TAP residente sem assinatura, WinEvent
hook ou timer; apenas o canal passivo de comandos continua disponível.

## Decisões de integração

- C# chama `InitializeXamlDiagnosticsEx` diretamente por P/Invoke, em thread
  STA dedicada. `CLSID` passa por valor; `initData` é o diretório absoluto de
  temas. DLL do sistema carregada com busca restrita a System32.
- Config compatível com o CLI, UTF-8 e substituição atômica no mesmo diretório.
  Preservar campos desconhecidos e `logLevel`; JSON inválido não autoriza
  sobrescrever a configuração. Só sinalizar depois de gravar com sucesso.
- `Ativo` significa configuração habilitada e pedido de carga/recarga aceito.
  Não há confirmação TAP→Tray. Falhas por regra continuam no log.
- `TaskbarCreated` inicia recuperação e recompõe o ícone. Verificação de
  segurança a cada 30 s encontra a taskbar pelo HWND, compara identidade do
  processo e detecta TAP ausente. Uma falha fica bloqueada até ação explícita
  ou novo `TaskbarCreated`; polling não repete a carga falha.
- Exportação nova usa um segundo Event de solicitação, na mesma direção
  Tray→TAP. É uma extensão explícita de §4.2, sem canal de retorno. O TAP
  serializa restauração/parada da assinatura, snapshot e retomada do tema;
  não executa o snapshot concorrente com a assinatura permanente. O custo de
  restaurar/reaplicar ocorre apenas nessa ação de diagnóstico.
- Diagnóstico mostra a última observação de registros sem liberação confirmada,
  com identidade do processo e idade; log ausente/antigo/incompleto não vira
  zero. Não há API confirmada para consultar o total interno do XAML.
- Menu `Desativar e sair` solicita reset antes de terminar. Não configura
  inicialização automática nem modifica pré-requisitos globais ao iniciar.

## Tasks e contratos

### 1. Ledger puro de registros de diagnóstico

Arquivos: `src/tap/handle_ledger.h/.cpp`, `tests/tap/test_handle_ledger.cpp`.

Ledger de dados puros, thread-safe, sem COM sob lock. Chave por proprietário
lógico da sessão e handle não zero. Relatórios duplicados não inflam o total.
Uma revisão protege confirmação tardia: reentrada durante a liberação não pode
apagar um relatório mais novo. Falha, hooks ausentes ou sessão retirada não
equivalem a liberação. Snapshot coerente explicita incompletude e residuais.
Identidade de wrapper não deve ser anunciada como cache físico independente.

Teste primeiro: duplicação/zero, re-report após release, falha, sessão antiga,
reentrada e concorrência. Commit: `feat(tap): contabiliza registros observados
sem liberacao confirmada`.

### 2. Reset inerte, encerramento serializado e integração do ledger

Arquivos: `src/tap/theme_session.*`, `change_subscription.*`, `tap_boundary.cpp`,
`release_queue.*`, `visual_tree_watcher.*`, `tree_export.cpp`, `thread_init.*`,
`ipc.h`, `log.cpp`, `report_dispatch.h`, CMake nativo e testes de ciclo de vida.

O callback do pool de reload só posta para a janela privada da UI; nunca espera
essa UI. Mensagem leva geração; parada invalida mensagens antigas. `Stopping`
permanece publicado até o fim real de Advise/Unadvise, inclusive no segundo
pedido de parada. Falha de Unadvise não permite trocar sessão/assinar novamente.
Nenhum callback XAML libera handles. Fila carrega o proprietário correto,
conserva handles retidos e os drena após reset mesmo sem novos relatórios.
Timer é parado e revogado na própria UI. Hook é removido na thread instaladora,
com resultado verificado. Apply posterior reativa a infraestrutura.

Relatórios do snapshot, da assinatura e das threads ainda não inicializadas
entram no ledger antes de retornos antecipados. Confirmar baixa apenas depois
de `UnregisterInstance` bem-sucedido pelo proprietário capturado. Publicar
observação no log com PID e criação do processo, preservando residuais.

Export Event usa a mesma serialização; não abre nova sessão COM apenas para
exportar. Reset durante export/reload prevalece sobre uma retomada antiga.
Árvore incompleta não substitui o arquivo válido. Escritas do log convivem
com o leitor compartilhado da bandeja. Relatórios anteriores à inicialização
entram na fila da UI de origem sem habilitar estilos.
Testes com barreiras verificam segundo Stop, falha de Unadvise e mensagens
antigas; teste de fila verifica retained→reset sem novo relatório.
Commit: `fix(tap): encerra assinatura e recursos antes de ficar inativo`.

### 3. Núcleo puro C# e persistência

Arquivos: `src/tray/Core/`, `tests/tray/`.

Biblioteca `net10.0`, sem WinForms. Máquina de estados Ativo/Inativo/Falhou/
Aguardando; resultados identificados por geração não alteram estado novo.
Catálogo valida metadados e IDs de arquivos; regras XAML permanecem opacas.
Config atômica preserva campos e recusa JSON inválido. Leitor de observações
suporta log rotacionado e ignora linha parcial ou processo anterior.

Testes comportamentais executáveis com .NET, sem dependências externas:
falha não é repetida pelo poll; novo shell/ação permite tentativa; resultado
antigo descartado; reset não carrega TAP ausente; configuração não se perde
num erro; observação ausente/antiga não vira zero.
Commit: `feat(tray): adiciona estado persistencia e diagnostico testaveis`.

### 4. Bandeja WinForms e adaptador Windows

Arquivos: `src/tray/` exceto `Core/`.

`ApplicationContext`, `NotifyIcon`, janela top-level oculta para receber
`TaskbarCreated`, menu nativo de temas e janela de diagnóstico não modal.
Instância única por sessão. Adaptador P/Invoke valida pré-requisito sem escrever
no registro, usa Event por operação e distingue ausência de acesso negado.
STA de carga não bloqueia o loop WinForms. Alterações de estado/resultado são
serializadas na UI; shutdown não libera recursos usados por trabalho em voo.

Menu: temas, desativar, tentar novamente, exportar árvore, diagnóstico, abrir
log, reiniciar Explorer e desativar/sair. Export só anuncia arquivo novo após
mudança confirmada do artefato; pedido enviado não é arquivo concluído.
Commit: `feat(tray): integra menu e recuperacao do Explorer`.

### 5. Distribuição e CI

Arquivos: `global.json`, `tools/build-tray.ps1`, `.github/workflows/ci.yml`,
`README.md`, `AGENTS.md` e `docs/smoke-test.md`.

Publicação x64 dependente do runtime .NET 10 instalado. Pacote local contém
Tray, TAP, temas e licença/créditos; nenhum fallback para o checkout.
CI compila/testa C#, publica e verifica os arquivos além das suítes existentes.
Lançar cópia isolada em caminho com espaços durante o smoke.
Commit: `build: empacota bandeja x64 e valida no CI`.

### 6. Revisão independente e validação em execução

Todas as suítes nativas/C#/Python verdes e `/W4` sem warnings novos antes dos
commits. Revisão por task de tempo de vida, fronteiras e estados; correções
atômicas separadas quando necessárias.

Smoke real: selecionar tema, trocar, abrir/fechar Task View, desativar e
confirmar parada de assinatura/hook/timer; aplicar novamente; reiniciar
Explorer com Tray ativo e confirmar recuperação sem intervenção; provocar
falha de carga controlada e confirmar que o poll não insiste; retry explícito;
exportar árvore nova e exibir diagnóstico; segunda instância não cria outro
ícone; sair e restaurar config original.

Preservar evidências e atualizar STATUS/decisões. Não declarar segundo monitor
físico nem estabilidade de 24 h validados por testes curtos. Integração local
em `main` só por fast-forward após conclusão; sem push.
