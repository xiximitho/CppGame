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

### 2. ~~Escadas / rampas~~ — FEITO
Escada existe: flags de item `StairsUp`/`StairsDown` (ids 103/104), regra em
`sim::World::apply_tile_transition`, e a `assets/maps/torre.txt` com os três andares
ligados e verificados. A troca de andar é instantânea e dispara na **chegada por
passo** — ver o invariante no CLAUDE.md e `tests/test_stairs.cpp`.

**Portal/warp usa a mesma regra e existe**, com destino absoluto autorado por tile
(`portal x y z dx dy dz` no mapa) — a decisão é um mapa só, grande, com o portal
levando a uma coordenada dele, e não vários mapas. Falta a UI de autoria no editor;
ver [maps.md](maps.md) e [pendencias.md](pendencias.md). Rampa (mudança de altura
sem teleporte) continua não existindo.

O que sobrou desta linha: rampa, e o **editor mostrando/editando só o andar 0** —
dá para pintar a escada, mas não o andar onde ela chega. Ver
[maps.md](maps.md).

### 3. Persistência — **feito**
Posição, facing, hp/max_hp, equipamento e inventário sobrevivem a um restart, em
`players.db` (SQLite, gitignored, separado do `content.db` que é conteúdo
commitado). Salva no logout, periodicamente (`--save-every`, 60s por padrão) e no
desligamento por SIGINT/SIGTERM. Schema versionado por `PRAGMA user_version` desde
o primeiro save, como esta seção exigia. Ver `store/players.hpp` e
[content.md](content.md).

O que **não** está feito e é decisão à parte: **autenticação**. O `HelloMsg` carrega
um nome e nenhuma credencial, então hoje o nome É a identidade — aceitável num
scaffold em rede confiável, inaceitável fora disso. Precisa de coluna de senha, um
KDF de verdade e mudança de protocolo.

Também não feito: mais de um personagem por conta (o schema permite, a query não),
e nenhum stat além de hp.

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

### 8. Chat, NPCs, quests
O conteúdo do jogo. Combate, inventário e mobs com classes já existem (ver
[monsters.md](monsters.md)); `sim::update_wanderers` foi deletado quando
`update_monsters` chegou, como estava previsto aqui. Falta chat, NPC que conversa
(hoje todo mob é hostil) e qualquer noção de progressão.

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
| Andar de cima do ator | desenhado translúcido, não escondido | opaco esconde o personagem atrás de um overhang; ver comentário em `world_render.cpp` |
| Mob | não conversa, não tem nível, loot é um item fixo por classe | tabela de loot e NPC são item 8 |
| `sim::Pathfinder` | planeja ignorando outros atores | atores se movem, então desviar deles produz rota que já está velha quando é andada; bloqueio é tratado no passo, com desistência após 1s |
| `CPathFollow` | não recalcula rota ao ser bloqueado, desiste | replanejar precisa de limite de tentativas para não virar busca por tick |
| Custo da diagonal | 1.5× no grid, mas `ScreenRelative` faz `W` ser diagonal | ver comentário em `input.hpp`: escolher esquema e achatar o custo |
