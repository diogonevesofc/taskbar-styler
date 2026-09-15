# Plano 5 — interface de temas e prévia

Pedido do dono: melhorar a aparência e a UX do aplicativo, com prévia da
taskbar antes de aplicar. Base `40b7634`; branch `plano-5-interface-preview`.

Implementação, revisão e validação concluídas. Registro:
`../plano-5-decisoes.md`. Prévia própria ilustrativa foi a opção entregue.

## Contrato

Janela nativa WinForms/.NET 10 com busca, catálogo e prévia separada da aplicação.
A execução inicial, segunda execução e abertura pela bandeja mostram o catálogo.
Fechar a janela mantém o aplicativo na bandeja; sair continua uma ação explícita.
Diagnóstico e operações técnicas ficam em acesso secundário. A seleção nunca
grava config nem sinaliza o TAP. Aplicar é explícito; feedback distingue pedido
aceito de falha, sem prometer confirmação visual que o IPC não fornece.

Prévia offline: preferir referências reais do upstream se disponíveis com
procedência. Identificar referência/ilustração e seu limite; não simular uma
captura fiel do desktop atual. Tema sem imagem continua selecionável, com estado
de ausência claro. Sem acesso à rede em runtime, mudanças no TAP ou dependências
globais novas.

## Tarefas

1. Estado puro de busca/seleção, perfil ilustrativo e testes comportamentais. Arquivos novos em
   `src/tray/Core/` e `tests/tray/`. Commit `feat(tray): separa selecao de tema da aplicacao`.
2. Catálogo visual, prévia e integração com ApplicationContext. Arquivos de UI
   em `src/tray/`, ativos e empacotamento quando necessários. Commit
   `feat(tray): adiciona catalogo visual com previa antes de aplicar`.
3. Revisão independente, build, testes e smoke da janela real: busca, seleção
   sem efeitos, aplicação explícita, falha, teclado, resize, reabertura e saída.
   Registrar screenshots e evidências, atualizar spec/STATUS/README. Commit
   `docs: registra interface e validacao do plano 5`.

## Direção visual

Utilitário de personalização para Windows: tipografia Segoe UI, fundo neutro,
hierarquia clara, um acento verde-petróleo, áreas de interação generosas e foco
visível. Catálogo à esquerda; nome, autoria e imagem à direita; rodapé estável
com estado atual e aplicar. Não acrescentar configurações sem implementação.

Integração local por fast-forward após revisão e verificações; sem push.
