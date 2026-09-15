# Plano 5 — interface e prévia

Data: 2026-09-15. Base `40b7634`, branch `plano-5-interface-preview`.
Pedido: melhorar aparência/UX e permitir prévia antes de aplicar.

## Entrega

WinForms/.NET 10 preservado, com janela de catálogo, busca por nome/autor,
prévia em destaque, autoria, indicação do tema configurado e ações explícitas.
Fundo neutro, acento verde-petróleo, tipografia Segoe UI e foco de teclado.
Nome de variante é apresentado com separador, mantendo o ID original de carga.
Diagnóstico recebeu espaçamento e controles consistentes, em acesso secundário.

Janela abre ao executar, antes do await da carga nativa; segunda execução e
clique esquerdo no ícone apontam para o catálogo. Fechar/Escape esconde a
janela; Desativar e sair continua ação explícita no menu de ferramentas.
Selecionar no antigo submenu agora abre a prévia, sem aplicar imediatamente.

## Prévia

Os JSONs não incluem imagens. A entrega usa desenho próprio offline com
quatro layouts (barra inteira, dock, blocos separados, pílulas), cores,
opacidade e cantos extraídos de um subconjunto das regras. Constantes têm
expansão limitada; recursos dinâmicos usam paleta neutra. Fundo claro/escuro
afeta apenas a ilustração. Não foram incorporadas imagens externas.

A legenda é explícita: não é screenshot da barra atual nem renderizador XAML.
Geometria, ícones fictícios e vidro são simplificados. O preview não acessa o
Explorer. Um perfil indisponível tem estado próprio; não depende de interpretar
o texto da descrição.

## Revisão e correções

- Busca/seleção e tema configurado independentes, inclusive refresh de catálogo.
- Busy bloqueia aplicar/reset; falha permite retry sem depender de config
  ainda conter o tema anterior. Corrigido reset cujo envio falha após salvar vazio.
- Materializar uma string JSON com surrogate isolado ou UTF-8 inválido pode
  lançar InvalidOperationException. Reproduzido antes da correção; agora o
  boundary de preview retorna indisponível sem falhar toda a bandeja.
- Ajustadas alturas do cabeçalho/rodapé após screenshot real. Glyph ausente
  substituído por ícone Fluent presente na fonte instalada.
- Empacotamento conserva a DLL nativa quando origem/destino têm SHA idêntico,
  permitindo atualizar só UI com a DLL já carregada. DLL diferente continua
  exigindo cópia bem-sucedida; erro não é ocultado.

Revisões independentes do modelo, parser, integração, desenho e lifecycle da
janela encerradas sem achados materiais pendentes.

## Verificação

- Build e publish Release: zero warnings/erros; sem dependências novas.
- C# **58/58** testes: 33 anteriores, 13 de catálogo/seleção, 12 de preview.
- CTest core/TAP: **2/2 suítes** verdes (129/7808 e 55/282).
- Harness STA sobre o DLL publicado: **23/23** verificações, incluindo seleção
  sem callbacks, apply explícito, busy, retry, vazio e resize. Em 940×700 a
  prévia mede 582×226; em 1440×960 mede 1082×486. Após 100 trocas, GDI **53→53**.
- Smoke real: busca Pills e fundo claro preservaram o hash da config. Aplicar
  Pills às 06:05:54; log registrou 66 elementos/301 propriedades/0 failed.
  Reset às 06:06:40 encerrou assinatura/hook/timers. DockLike reaplicado às
  06:07:42 com 16 elementos/26 propriedades/0 failed. Explorer permaneceu
  PID **20712**, criação **134339356730071250**.
- Escape ocultou a janela; executar novamente a abriu, mantendo a instância
  PID 19092. Após publicação final, nova instância da versão entregue aberta.
- Config original `DockLike` restaurada byte a byte: SHA
  `407F79ECC61D7EB01F876F9B74F178E9897C81A44A7F3EFF85BCDF28FC5B33DE`.
- DLL Tray final:
  `137D0E8F9135243C8E8239EB9B07BF08EFDC99C1E9ED008CA3246B319AA06E55`.
- TAP inalterado:
  `93DA4A2EFD9B493BC3631601D42228E45FB36072393AF43C00C8EF5D344101D2`.

Screenshot entregue em `docs/images/theme-browser.jpg`. Evidências locais em
`.superpowers/sdd/2026-09-15-plano-5-interface-preview/`; harness de UI em
`.superpowers/sdd/2026-09-14-plano-4-app-bandeja/browser-harness/results-published/`.

## Limites

Clique físico no ícone e matriz de monitores/DPI/hot-plug continuam pendentes;
resize e escala do desenho não substituem hardware real. Não houve ensaio de
24 h nem push/CI remoto. O ledger nativo desta sessão marcou `incomplete=1`,
inclusive no reset (observed=0/residual=0); não se declara cobertura completa
ou ausência universal de vazamentos. Nenhum código do TAP foi alterado.
