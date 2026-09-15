# Plano 3b — reprodução do crash do Explorer

Registro de 2026-09-14. Horários locais em UTC−03:00.

**Resultado inicial: crash reproduzido com dump completo.** A primeira rodada
terminou durante o terceiro reset. A correção do alcance do reset está em
`bfe330d`; a reexecução completou os **30 pares e 5min24s finais em reset**,
sem novo crash. O gate foi encerrado no cenário executado e a configuração
original da máquina foi restaurada e verificada.
A relação exata entre o estado retido e o HWND rejeitado pelo XAML não foi
demonstrada no dump; os resultados comparativos estão separados abaixo.

## Autorização e captura validada

O dono da máquina autorizou explicitamente configurar dumps completos e executar
os reinícios do Explorer. A configuração elevada terminou às **20:08:20**:

- Chave de 64 bits
  `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\explorer.exe`.
- `DumpType=2`, `DumpCount=3`, pasta
  `%LOCALAPPDATA%\TaskbarStyler\Dumps\plano-3b-20260914`.
- Chave equivalente para o executável isolado de validação
  `TaskbarStyler.WerProbe.exe`.

O estado original foi preservado: ambas as chaves estavam ausentes; `WerSvc`
estava **Disabled/Stopped**. O primeiro probe, PID **4988**, iniciado às
20:11:36, não produziu dump. Às **20:12:07**, o serviço foi configurado como
**Manual** e iniciado. O segundo probe, PID **4692**, iniciado às 20:12:23,
produziu um dump de **10.360.600 bytes**. A leitura do arquivo confirmou
assinatura `MDMP` (`0x504d444d`), PID 4692 e o sinalizador de memória completa
(`flags=0x21826`). Essa verificação confirmou a captura funcional antes do
ensaio do Explorer; a existência da chave, isoladamente, não teria bastado.

Esse foi o estado usado na captura. A restauração foi executada às 20:55:46 e
verificada às 20:56:30, conforme o fechamento operacional ao final.

## Artefato executado e preservado

Foi usada a DLL das correções em `27ff1a8`, com documentação em `c2a3607`.
Os binários do crash foram copiados antes de qualquer nova compilação. Os hashes
das cópias em `crash-binaries/` coincidem com os coletados antes do ensaio:

| Artefato | SHA-256 |
|---|---|
| `TaskbarStyler.Tap.dll` | `E7B90B57D2B566E1ACD7EC0F5CADED8A40B4378C56F17F74AA1998E1B442724A` |
| `TaskbarStyler.Tap.pdb` | `9D6C985538D031180D7243FE33AE18E2EF25C34A98F4BA89CA1934756BC141C9` |
| `explorer.exe.16800.dmp` | `378DA1B0B38D6A0F170B3380BFF5D4C23EC0412478985C19B448EED094304D54` |

O evento de falha identifica Explorer e `Windows.UI.Xaml.dll` na versão
**10.0.26100.9444**. Os resultados abaixo se limitam a essa máquina e a esse
artefato.

## Roteiro e preparação

O roteiro escolhido foi três processos novos do Explorer, dez pares
`apply Command_Center`/`reset` em cada um, criação de hosts confirmada em ambos
os estados e pelo menos cinco minutos finais em reset. Essas contagens delimitam
o ensaio; não são um limiar estatístico nem uma exigência numérica do plano.

As identificações **1 e 2** no journal correspondem à preparação do harness.
Houve falhas de confirmação por procurar as estatísticas antes de um novo evento
de árvore produzir o primeiro dreno. Uma retomada reutilizou o PID da tentativa
2. A identificação **20** registra a exploração de estímulos nesse processo.
Essas tentativas não contam como sessões completas do gate.

O harness passou a exigir confirmação nova por operação, preservar logs ao
falhar e exigir cobertura de hosts. Foram usados hover em probes Win32,
acionamento do volume e abertura/fechamento de Task View por UI Automation.
Um probe adicional abria e solicitava fechamento de uma janela para provocar
eventos de árvore e o dreno. O evento `pulse-close` registra a solicitação de
fechamento, não uma confirmação de término do processo.

**Task View produziu hosts novos de forma observável.** Volume e hover não
produziram hosts novos nas fases de apply da sessão que falhou. Assim, a
cobertura exigida em ambos os estados ainda não estava atendida.

## Sessão 3 — sequência da falha

O restart intencional foi registrado às **20:22:41.632**, encerrando o PID
23880. O novo Explorer, **PID 16800**, iniciou às **20:22:41.699**. O TAP foi
carregado às **20:22:52.133**, com thread principal **4844**. Não há outro
restart intencional registrado nessa sessão.

| Horário | Ação ou evidência |
|---|---|
| 20:22:57.597 | Apply 1: 72 elementos, 296 propriedades, 56 brushes, 0 falhas e 0 fallbacks na thread 4844. |
| 20:23:01.022–01.049 | Reset 1: 74 elementos restaurados, tema vazio e assinatura reiniciada. |
| 20:23:04.684 / 04.707 | Dois hosts após Task View; thread 24196 inicializada às 04.703. |
| 20:23:10.289 | Apply 2 também alcançou a thread 24196: 52 elementos, 314 propriedades, 46 brushes, 0 falhas e 0 fallbacks. |
| 20:23:15.437 | Apply 2 na principal: 74 elementos, 304 propriedades, 60 brushes, 0 falhas e 0 fallbacks. |
| 20:23:18.744–18.753 | Reset 2: 74 elementos restaurados na principal, tema vazio e assinatura reiniciada. |
| 20:23:22.361 / 22.392 | Dois novos hosts após Task View. |
| 20:23:32.912 | Apply 3 na principal: 74 elementos, 304 propriedades, 60 brushes, 0 falhas e 0 fallbacks. |
| 20:23:36.204 | Reset 3 restaurou 74 elementos na principal. |
| 20:23:36.206 / 36.232 | `no theme configured`; nova assinatura na thread 5928. |
| 20:23:40.093 | Terceiro acionamento de Task View, com estado inicial `Off`. |
| 20:23:40.108 | Novo host `00000000003C0130`. |
| 20:23:40.129 | Dreno da principal: 0 elementos, 0 propriedades, 0 falhas e 0 brushes. |
| 20:23:40.135 / 40.148 | Hosts `0000000000520754` e `00000000007C0BD8`; a última linha do TAP termina às 40.148. |
| 20:23:40.496 | Evento Application Error 1000 para o PID 16800. |
| 20:23:42.320 | Harness interrompido: uma chamada UIA `Toggle` retornou `Unrecognized error`. |
| 20:23:44.965 | Evento Winlogon 1002, confirmando interrupção do shell. |
| 20:23:45.006 | Início do Explorer substituto, PID 17984. |

Contagem efetiva: **três applies e três resets**, todos com retorno zero da CLI
e estatísticas novas na thread principal. O harness registrou **cinco fases
completas**: dois pares e o terceiro apply. O terceiro reset foi aplicado, mas
sua fase de estímulos terminou no crash; não é um terceiro par aprovado.

Foram registrados **sete hosts novos**, todos após reset: **2 + 2 + 3**.
As **72 linhas** do PID 16800 contêm **zero `ERR`**. Isso não impediu a falha.
A cópia feita na exceção e o log corrente eram idênticos por SHA-256 na auditoria:
`6ABEDC2B3048B60566A09BD3C83906C2A188BAD636C7555D119EBD4AE61CCC8E`.
A geração `.1` não contém linhas desse PID; não houve perda de uma geração da
sessão entre esses arquivos.

O timestamp do evento 1000 fica aproximadamente **348 ms** após a última linha
de host. Esse é o intervalo entre registros, não uma medição do instante exato
da exceção. O ensaio não comprova o intervalo de 100 ms citado na hipótese
histórica. A semelhança com o incidente das 14:13 também não estabelece a mesma
causa.

## Dump e atribuição

O dump completo tem **738.013.155 bytes**, criado às 20:23:42 e concluído às
20:23:44. A análise inicial confirmou:

- Exceção **`0xC000027B`** no PID **16800**, thread **24196**.
- Endereço registrado: **`Windows.UI.Xaml.dll+0x90e383`**, também identificado
  no evento Application Error 1000.
- Exceção armazenada (*stowed*) com **`HRESULT 0x80070057`**, thread **24196**.
- A DLL do TAP estava carregada no processo.

O PDB Microsoft correspondente a `Windows.UI.Xaml.dll` foi carregado por um
cliente DbgEng compilado com o SDK já instalado. A pilha armazenada identifica:

```text
CDirectManipulationService::ActivateDirectManipulationManager
CInputServices::UpdateDirectManipulationManagerActivation
CUIDMContainerHandler::NotifyCanManipulateElements
ScrollViewer::OnManipulatabilityAffectingPropertyChanged
ScrollViewer::put_ManipulationHandler
CInputServices::InitializeDirectManipulationContainers
CXcpDispatcher::Tick
```

O disassembly localiza o retorno inválido em
`IDirectManipulationManager::Activate(m_hWnd)`. O encerramento passa por
`DirectUI::ErrorHelper::ProcessUnhandledError`. Não há frame TAP na pilha
armazenada nem nas 112 pilhas correntes examinadas. Isso não exclui efeitos
assíncronos de referências ou propriedades alteradas anteriormente. O HWND
exato passado à chamada que falhou não foi recuperado; não atribuir a causa
ao compositor do blur com base nesse dump.

### Defeito confirmado no alcance do reset

A versão do crash restaura diretamente a thread principal e despacha o reset
para os hosts encontrados por `GetXamlHostWnds()`. Essa função usa
`EnumWindows` e seleciona classes de hosts top-level. Uma thread já estilizada
pode continuar viva com estado `thread_local` depois que seus hosts top-level
deixam de existir. Nesse caso, a enumeração não fornece uma janela pela qual
despachar sua restauração.

Esse é um defeito verificável no alcance do reset, independentemente da causa
do crash. `EnumWindows` também inclui janelas ocultas: a limitação não é
simplesmente estar invisível, mas deixar de ter um host top-level enumerável.
No caso observado, a thread **24196** recebeu estilos e os resets seguintes
registraram restauração apenas na principal **4844**. Esse histórico é
compatível com a lacuna; ausência de uma linha, isoladamente, não provaria a
causalidade do crash.

A correção usa uma janela própria **`HWND_MESSAGE`** por
thread inicializada como destino persistente do despacho, separando o alcance
do reset da existência momentânea dos flyouts. As janelas são enumeradas pela
classe privada e pelo PID, sem cadastro global de TIDs. Sua destruição não faz
COM/XAML. Um reset incompleto mantém o tema retirado e impede a substituição de
tema/sessão, inclusive numa nova chamada de `SetSite(site)`.

Os callbacks deixam de reaplicar um tema retirado enquanto a restauração ainda
está alcançando a thread. O callback de propriedade continua capturando o valor
mais recente escolhido pelo shell, para que a restauração não o substitua por
um valor anterior.

Dois testes Win32 verificam endpoint após fechamento do host, deduplicação,
limpeza na saída da thread e falha de callback sem escapar do loop. Build sob
`/W4` sem warnings novos; core **129/129** (7808 asserções), TAP **32/32**
(109 asserções), Python **22/22**. A revisão independente foi encerrada após
corrigir a captura do valor original e a barreira de recarga. A nova DLL tem
SHA-256 `9EE5F1BAC8FE23F86C77B73F63F0E535C2833AB62871CCB5EF5E8E852EF25DE3`.
O módulo carregado foi conferido em `build/src/cli/TaskbarStyler.Tap.dll`;
o PDB correspondente tem SHA-256
`D2762AC9B8DEEC9223BF3437FBB30863637A900C7BCBC8D3CBAFEFC2F6030118`.
Os resultados da reexecução estão na seção seguinte aos controles.

## Controles posteriores

Os dois controles usaram o Explorer substituto **PID 17984**:

| Controle | Intervalo | Resultado |
|---|---|---|
| Sessão 90: Task View sem TAP | 20:28:07–20:28:36 | Dez ciclos concluídos; zero módulos TAP no início; PID preservado. |
| Sessão 91: Task View com TAP carregado e tema vazio | 20:29:52–20:30:23 | Dez ciclos concluídos; PID preservado. |

Esses controles não reproduziram a falha nesses intervalos. O segundo não
repete o histórico de aplicar estilos à thread 24196 e depois restaurá-los;
portanto, não elimina a hipótese de estado remanescente após apply/reset. Nenhum
dos controles substitui o ensaio adversarial que falhou.

## Reexecução com a correção

O mesmo artefato corrigido foi usado nas três sessões. Cada fase abriu e fechou
Task View e uma janela isolada `T7-D`, confirmando estatísticas novas na thread
principal depois do estímulo. As novas janelas próprias de despacho permitiram
restaurar a thread de Task View mesmo com seus hosts fechados.

| Sessão / PID | Intervalo dos dez pares | Hosts em apply | Hosts em reset |
|---|---|---:|---:|
| 101 / 25616 | 20:37:34–20:39:47 | 20 | 20 |
| 102 / 24572 | 20:40:15–20:42:26 | 21 | 20 |
| 103 / 25984 | 20:44:24–20:46:29 | 20 | 20 |

**30 pares, 60 fases confirmadas e 121 hosts novos**, com zero `ERR`, falhas
de propriedades ou fallbacks do TAP nesses processos. As threads auxiliares
17900, 5676 e 10052 foram alcançadas pela restauração antes da troca da
assinatura. Os PIDs permaneceram iguais dentro de cada sessão; as trocas entre
sessões foram intencionais e registradas antes do encerramento.

Houve uma falha adicional **antes de carregar o TAP** na sessão 103:
às 20:42:53, a CLI retornou `0x80070490 (ERROR_NOT_FOUND)`. O Explorer 25984
continuou responsivo, sem TAP carregado e sem novo dump. Após inspeção, uma
nova tentativa explícita no mesmo PID carregou `VisualDiagConnection1` às
20:44:24. Os dez pares da tabela ocorreram depois dessa carga. Não houve
alteração no loader nem adição de retry automático. A causa exata dessa
indisponibilidade inicial não foi confirmada; não declarar que todas as
tentativas de carregamento passaram.

A observação final em reset, sessão 104, foi de **20:46:48.693 a
20:52:12.888**, totalizando **324,208 segundos**. As 22 amostras mantiveram
o PID 25984 e zero `ERR`. Task View foi acionado após **289,178 segundos**;
dois hosts foram registrados às **20:51:37.968** e **20:51:37.980**. Houve
cerca de 35 segundos adicionais de acompanhamento após o estímulo, sem falha
de propriedade ou fallback positivo no log. O total é **123 hosts**: os 121
dos trinta pares e dois tardios. Os 42 hosts no log final desse PID incluem
os 40 da sessão 103; não são 42 adicionais.

## Fechamento operacional

A conferência de dumps e eventos às **20:56:48** encontrou apenas os dois dumps
preservados, do probe e do crash PID 16800. Não houve novo Application Error
1000 do Explorer desde 20:35. Os eventos Winlogon 1002 de 20:35:07,
20:40:05 e 20:42:42 correspondem aos reinícios intencionais registrados no
journal antes dos comandos; nenhum indica mudança de PID dentro dos pares.

O script de restauração terminou às **20:55:46.412**. A verificação independente
às **20:56:30.419** confirmou:

- Ausência das duas chaves de LocalDumps, como no estado original.
- `WerSvc` novamente **Disabled/Stopped**; demais políticas de WER não alteradas.
- Config original com tema vazio e log debug, SHA-256
  `FFA61D589CCBCD4836DA527FD21049CBA8D071A70384AACA67ACA776F47255BF`.
- Reset adicional confirmado nas threads 13484 e 10052, PID 25984 preservado
  e nenhum probe restante. A captura `adversarial-final-reset.png` mostra o
  visual padrão da taskbar.

As verificações estão em `Restore-result.json`, `restoration-verified.json`,
`restored-final.log` e `final-events-dumps.json`. O config original, os dumps
e os binários correspondentes continuam preservados localmente.

## Evidências e condição para prosseguir

O scratch, ignorado pelo Git, fica em
`.superpowers/sdd/2026-09-14-plano-3b-fidelidade/adversarial-20260914/`.
Os três pontos de entrada para a investigação são:

- `journal.jsonl`: comandos, PID esperado, fases, estímulos e controles.
- `crash-16800-log.txt`: log preservado do crash, incluindo a sequência acima.
- `%LOCALAPPDATA%\TaskbarStyler\Dumps\plano-3b-20260914\explorer.exe.16800.dmp`:
  dump completo; manter junto das cópias da DLL e do PDB identificadas pelos
  hashes deste documento.

**Gate concluído no cenário executado, com integração local por fast-forward.**
Correção, revisão, suítes, trinta pares, observação final e restauração estão
concluídos. Não houve push. A ausência de reprodução após a correção é evidência
limitada a este ensaio, não prova da causa exata do crash capturado ou histórico.

O contador `held` continua sendo por lote de dreno. Os valores zero observados
não demonstram ausência de handles XAML vivos, de referências retidas ou de
estado remanescente em outra thread. Segundo monitor, outros temas e outras
versões do Windows permanecem fora desta evidência.
