# Plano 6 — distribuição pública

Pedido explícito: distribuir gratuitamente, criar instalador e publicar
instalador/código-fonte no GitHub com README apropriado. Publicação e push
autorizados nesta rodada. Base `96094c7`; branch `plano-6-distribuicao`.

## Entrega

Primeira versão pública **0.1.0-beta.1**, Windows 11 x64, sem declaração de
estabilidade ainda não medida. Instalador Inno Setup, runtime .NET incluído,
TAP/CLI em Release com CRT estático. Sem instalação de ferramentas globais
na máquina de desenvolvimento; o compilador do instalador roda no CI.

Instalação em Program Files com UAC e descrição do pré-requisito de diagnóstico
XAML. Sem autostart ou reinício forçado do Explorer. Atalho de Iniciar e
desinstalador; configuração/logs preservados. Arquivos em uso podem exigir
reinício para substituição/remoção, conforme fluxo do Windows.

## Tarefas

1. Versionamento, publicação self-contained, avisos/licenças e artefato nativo
   Release. Build separado; confirmar dependências e conteúdo do pacote.
2. Instalador e workflow para build/testes/instalação/desinstalação isolados
   no runner. Gerar checksum e guardar artefatos; publicar somente após testes.
3. README para usuários, instruções de contribuição/relato de bugs, fontes e
   limitações da beta. Auditoria de arquivos/histórico para publicação.
4. Testar runtime local, revisar independentemente, integrar por fast-forward,
   push, tag/release pública com binário validado e fonte correspondente.

Cada task tem commit separado. Não reescrever histórico existente nem afirmar
que a beta passou pela matriz de hardware ou observação de 24 h.
