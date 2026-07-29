# Roadmap

O que **não** existe ainda, em ordem de quanto custa adicionar depois versus agora.

Já feito: **A\* no grid de tiles** (`sim::Pathfinder`) com clique-para-mover
planejado e executado pela simulação — no multiplayer, pelo servidor.

## Caro de adiar (faça antes de escrever muito conteúdo)

### 1. Dear ImGui para ferramentas de debug
Hoje o HUD é o título da janela. Sem ferramentas, todo ajuste de balanceamento e
todo bug de render vira `printf`. É a coisa com maior retorno na lista.

`imgui` + `imgui_impl_sdl3` + `imgui_impl_sdlrenderer3`, no mesmo padrão de SHA
travado. Ver [dependencies.md](dependencies.md).

### 2. Escadas / rampas — andares superiores são inalcançáveis hoje
O mundo tem 3 andares, `TilePos` tem `z`, a projeção compensa altura, a ordenação
de desenho trata andar como dominante, andares acima do jogador são escondidos e
andares abaixo são escurecidos. **Mas não existe nenhuma forma de trocar de
andar.** `find_spawn_tile` sempre devolve `z = 0` e `tile_step` nunca muda `z`.

Ou seja: toda a infraestrutura multi-andar está pronta e nunca é exercitada em
jogo. Falta:

- um tipo de tile "escada" / "rampa" que ao ser pisado mova o ator para `z ± 1`
- a regra em `World::request_walk` (ou um sistema novo) que aplique isso
- decidir se a troca de andar é instantânea ou consome um passo

É pequeno e desbloqueia a única feature estrutural que já foi paga e não rende
nada. Enquanto não existir, o código de render multi-andar não foi visto
funcionando de verdade.

### 3. Persistência
Nada é salvo. Ao reiniciar o servidor, todo mundo volta pro spawn. Precisa de:
- conta e personagem (SQLite local ou Postgres)
- posição, inventário, stats
- salvamento periódico e no logout

Decidir isso depois de existir inventário e stats é mais barato do que decidir
antes, **mas** o formato de serialização precisa ser versionado desde o primeiro
save gravado em disco de jogador real.

### 4. Delta compression nos snapshots
Hoje cada snapshot é completo. Com 20 atores visíveis a 10 Hz dá ~3 KB/s por
jogador, o que é aceitável — e deixa de ser em uma cidade com 100 jogadores. O
formato de fio já isola isso (`write_snapshot`), então é uma mudança contida:
guardar por peer o último snapshot confirmado e mandar só o que mudou.

Junto com isso: **prioridade por ator**, porque `kMaxActorsPerSnapshot` hoje
trunca em 255 sem critério nenhum.

## Média prioridade

### 5. Client-side prediction
Não existe. A latência visível é o início de um passo. Em LAN não incomoda; em
internet com 80 ms, incomoda. Como o movimento é discreto, a predição aqui é mais
simples que em movimento livre: o cliente começa o passo localmente e o servidor
confirma ou corrige o tile. Precisa de número de sequência no input e
reconciliação por tile, não por posição.

### 6. Arte de verdade
O atlas é gerado em código (`tileset.cpp`). Trocar por arte exige:
- decodificador de PNG (`stb_image`)
- empacotador de atlas offline (`stb_rect_pack`) em `tools/`
- arquivo de definição de tileset carregado pelo `platform::vfs`
- 8 direções por ator, com frames de animação de caminhada

`AtlasEntry` já é a interface certa para isso: o renderer não muda.

### 7. Áudio
Nada. Recomendação: **miniaudio** (single header, funciona nos 5 targets) em vez de
SDL3_mixer, que é mais simples mas limita depois.

### 8. Combate, inventário, chat, NPCs
O conteúdo do jogo. `CHealth` existe e nada usa. `sim::update_wanderers` é um
placeholder explícito e deve ser deletado quando houver IA real.

## Baixa prioridade / quando doer

### 9. Renderer em SDL_GPU
O `SDL_Render` atual não aceita shader customizado. No dia que precisar de luz
dinâmica, palette swap, outline ou fog of war shaderizado, implemente
`Renderer2D` sobre SDL_GPU + SDL_shadercross. A interface já está pronta para
isso.

### 10. Ordenação topológica de sprites
Objetos maiores que um tile ordenam errado. Ver [architecture.md](architecture.md).

### 11. GameNetworkingSockets
Trocar ENet quando precisar de criptografia, IPv6 ou relay da Steam. É uma
segunda implementação de `net::ITransport`.

### 12. Mobile de verdade
Estrutura preparada, build não verificada. Ver [mobile.md](mobile.md).

---

## Coisas que estão erradas de propósito

Registradas para não serem descobertas como surpresa:

| Onde | O quê | Por quê está assim |
|---|---|---|
| `iso::depth_key` | objeto >1 tile ordena errado | painter's algorithm; topológico só quando houver prédio |
| `World::request_walk` | input durante passo é descartado, não enfileirado | fila de 1 slot dá caminhada encadeada mais responsiva; é ajuste de feel |
| `write_snapshot` | trunca em 255 atores sem critério | precisa de prioridade (item 4) |
| `SoloSession` | copia o `TileMap` uma vez | terreno destrutível precisaria de versionamento por chunk |
| `RemoteSession` | sem predição | item 5 |
| Andares 1 e 2 | existem no dado e no render, inalcançáveis em jogo | falta escada; item 2 |
| `sim::Pathfinder` | planeja ignorando outros atores | atores se movem, então desviar deles produz rota que já está velha quando é andada; bloqueio é tratado no passo, com desistência após 1s |
| `CPathFollow` | não recalcula rota ao ser bloqueado, desiste | replanejar precisa de limite de tentativas para não virar busca por tick |
| Custo da diagonal | 1.5× no grid, mas `ScreenRelative` faz `W` ser diagonal | ver comentário em `input.hpp`: escolher esquema e achatar o custo |
