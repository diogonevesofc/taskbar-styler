# Plano 3b — spike de reciclagem da taskbar

Data: 2026-09-14. Tema: `Pills`, log em `Debug`. Horários locais da máquina
(`America/Sao_Paulo`). Plano: Task 7 de
`plans/2026-09-14-plano-3b-fidelidade.md`.

## Decisão

**Sem defeito observável neste cenário.** A Task 7 termina sem código
adicional de detecção ou reaplicação. O teste não sustenta a afirmação
"a taskbar não recicla elementos": o conjunto inicial já continha botões
pré-criados e as trocas de aplicativos não geraram novos relatórios de
estilização desses botões raiz.

O critério que motivaria implementação era um botão substituído conservar um
estilo visual incorreto do item anterior. Isso não foi observado. Reaplicar
`Pills` depois da substituição também não mudou o resultado visual registrado.

## Cenário e procedimento

- Explorer PID **22488**, preservado durante o ensaio.
- Snapshot inicial às **19:38:03**: duas árvores com 12
  `Taskbar.TaskListButton#TaskListButton` cada e 24 linhas `styled` desses
  botões. A contagem descreve as árvores reportadas, não 24 aplicativos
  abertos nem dois monitores físicos.
- Janelas Win32 de teste foram criadas com o helper local `t7-window.cpp`.
  Cada executável usa `SetCurrentProcessExplicitAppUserModelID` com identidade
  `TaskbarStyler.Spike.<nome>`, evitando que as seis janelas fossem apenas um
  único grupo de aplicativo.
- Abriram-se **B, C, D, E, F, G**; fecharam-se **C, D, E**; abriram-se
  **H, I, J**. O estado final continha **B, F, G, H, I, J**.
- Depois das substituições foi reaplicado o mesmo `Pills`, como comparação
  visual com uma estilização completa nova. O ensaio terminou em `reset`.

## Evidências

Os arquivos abaixo estão no scratch local ignorado pelo Git:
`.superpowers/sdd/2026-09-14-plano-3b-fidelidade/`.

| Horário | Operação | Captura e log |
|---|---|---|
| 19:45:28 | Captura dos seis aplicativos abertos | `t7-six.png`, `t7-six.log` |
| 19:45:59 | Captura após fechar os três aplicativos do meio | `t7-three.png`, `t7-three.log` |
| 19:46:15 | Captura dos três substitutos, antes de reaplicar tema | `t7-replaced-before.png`, `t7-replaced-before.log` |
| 19:46:17–19:46:45 | Pedido de reaplicação de `Pills`; captura posterior às 19:46:45 | `t7-replaced-after.png`, `t7-replaced-after.log` |
| 19:47:26 | `reset` | Log do TAP: `restored 223 elements`, dreno com `0 held` |

Durante a abertura, fechamento e substituição não apareceram novas linhas
`styled Taskbar.TaskListButton#TaskListButton`; surgiram relatórios de
`RunningIndicator` e mudanças de estado visual. Após reaplicar `Pills`, houve
24 linhas novas de estilização dos botões raiz. As capturas antes/depois da
reaplicação mostram o mesmo resultado visual, sem pílula ou largura residual
incorreta observável.

**Zero linhas `ERR` do PID 22488** no período. O mesmo arquivo contém
`ERR x` às 19:44:13 e 19:46:21, emitidos pelos processos de testes **21996** e
**24768**, respectivamente. Esses registros não são erros do Explorer; a
triagem deve filtrar PID e nível do log, não contar a palavra `ERR` no arquivo
inteiro.

O reset registrou restauração de 223 elementos. O `0 held` seguinte descreve
somente o lote drenado (`unique_count - to_release.size()`), não a quantidade
global de handles vivos. Não é prova de ausência global de vazamento.

## Limites e condição para reabrir

O ensaio usou ícones agrupados, sem rótulos, e capturou a imagem de um monitor.
Os helpers têm identidades distintas, mas a aparência de ícone é genérica.
Isso limita a capacidade de distinguir estilos específicos por aplicativo.
Não foram comparados outros temas, versões do Windows, rótulos expandidos ou
taskbars de monitores adicionais.

A falta de novos `styled` é compatível com uso do conjunto de botões já
existente; não identifica por si só o mecanismo interno do `ItemsRepeater`.
Como não houve defeito visual, não foi adicionada instrumentação de
`DataContextChanged`/`Visibility`, nem projeção `Microsoft.UI.Xaml`.

Reabrir a task diante de reprodução com botão substituído visualmente
incorreto. Nesse caso, medir os sinais de reciclagem antes de implementar o
gatilho, mantendo o contrato de identidade por `IInspectable` da decisão 3.
Este spike não encerra o gate separado do reinício histórico do Explorer.
