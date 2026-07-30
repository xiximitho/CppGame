# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Jogo isométrico tile-based em C++20, estilo Tibia: mundo por tiles, múltiplos
andares, movimento passo-a-passo, um personagem por jogador, servidor autoritativo.
Alvos: Windows, Linux, macOS, Android, iOS.

## Comandos

```bash
# Primeira vez numa máquina (instala compilador, CMake, Ninja e headers do SDL3)
./scripts/setup-linux.sh          # ou setup-macos.sh / setup-windows.ps1

cmake --preset debug              # 1ª vez baixa as dependências para .deps/ (~2-5 min)
cmake --build --preset debug -j
ctest --preset debug
```

| Preset | Para quê |
|---|---|
| `debug` | dia a dia |
| `release` | `RelWithDebInfo` |
| `server-only` | servidor headless, **sem SDL3 nem lib gráfica** (e `GAME_BUILD_TOOLS=OFF`) |
| `asan` | ASan + UBSan; rode aqui ao mexer em `net/` |
| `ci` | release com `-Werror` |

```bash
# Um teste só (doctest)
./build/debug/bin/game_tests --test-case="*corner of two walls*"
./build/debug/bin/game_tests --source-file="*test_iso*"
./build/debug/bin/game_tests --list-test-cases

# Checagens que a CI roda
./scripts/check-layering.sh       # fronteiras de módulo — falha o build se violadas
./scripts/verify-deps.sh          # confere que cada SHA pinado existe e bate com o comentário

# Rodar
./build/debug/bin/game_client                          # solo
./scripts/run-local.sh                                 # sobe servidor + cliente
./build/debug/bin/game_client --connect 127.0.0.1:7777 --name felipe

# Autorar conteúdo — F2 alterna mapa <-> modo item
./build/debug/bin/game_editor
./build/debug/bin/game_bake                            # content.db -> content.bin

# Servidor com persistência
./build/debug/bin/game_server --players players.db --save-every 60
```

`-Werror` é **off** por padrão e **on** no preset `ci`. Antes de dizer que uma
mudança está pronta, compile com `-DGAME_WERROR=ON`: os avisos são agressivos
(`-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`, `-Wdouble-promotion`) e
esse código é limpo hoje — mantenha assim. Em particular, passar `float` para
`printf`/`LOG_*` exige `static_cast<double>` explícito.

### Conteúdo é dado, não código

Tipo de item novo (ou ajuste de attack/defense/alcance) **não é mudança de C++**:
é uma linha no `assets/content.db`, editada pelo `game_editor` (`F2` abre o modo
item; `N` cria, setas editam, campo `sprite` + `enter` escolhe o recorte no atlas,
`S` salva). O `S` grava no banco **e** regera o `assets/content.bin`. Receitas em
`docs/authoring.md`, o porquê em `docs/content.md`.

Quem lê o quê: **servidor lê o `content.db` direto**; **cliente lê o `content.bin`**
(no Android os assets ficam dentro do pacote e não são arquivos, e SQLite precisa de
caminho real). Editou fora do editor — SQL na mão, script? **Rode `game_bake`**, ou
o servidor rejeita o cliente com `content mismatch: re-run game_bake`. Isso é o
comportamento correto, não bug: ver o invariante do hash de conteúdo mais abaixo.

`assets/content.db` é commitado (é conteúdo); `assets/content.bin` é derivado e
gitignored; `players.db` é save de jogador e nunca vai para o git.

## Arquitetura

Documentação de fundo em `docs/architecture.md`, `docs/dependencies.md`,
`docs/mobile.md`, `docs/roadmap.md`. Para conteúdo e autoria: `docs/content.md` (o
pipeline e o porquê das decisões), `docs/authoring.md` (as receitas),
`docs/sprites.md`. **`docs/pendencias.md` é o handoff**: o que está pronto, o que
falta, e o que foi verificado de que jeito. O que segue é o mínimo para não quebrar
nada.

### A regra de camadas

```
core  <-  sim  <-  net  <-  server
  ^        ^        ^          ^
  |        +--------+----  platform  <-  client
  +----  store  ------------- + ------  tools (game_editor, bake)
```

`src/sim/` é o estado autoritativo e as regras. Ele **não pode** incluir SDL, abrir
socket, tocar filesystem nem ler relógio. `scripts/check-layering.sh` verifica isso
por grep de `#include` e por `SDL3::` nos `CMakeLists.txt`, e roda na CI.

`src/store/` é o wrapper de SQLite (`store::Db`). Servidor e ferramentas linkam;
**o cliente nunca** — no Android os dados dele estão dentro do pacote e não são
arquivos, e SQLite precisa de caminho real. O cliente lê conteúdo do blob baked via
`platform::vfs`. O `check-layering.sh` reprova `<sqlite3.h>` e link de
`sqlite3`/`game_store` em `src/client/` e `src/platform/`. Ver `docs/content.md`.

Não é estética. Três coisas dependem disso: o servidor compila sem lib gráfica
(preset `server-only`), os testes dirigem a simulação sem abrir janela, e o modo
solo não pode divergir do multiplayer. Se precisar de tempo dentro de uma regra,
use `sim::Tick` — o `World` é avançado um tick fixo por vez por quem o chama.

### Movimento: passo manual e rota

Duas entradas, e a diferença importa:

- `World::request_walk(id, dir)` é um **primitivo**. Não cancela rota, porque o
  próprio seguidor de rota o chama a cada passo. Quem trata input direto do jogador
  precisa chamar `World::cancel_path(id)` antes — é o que o handler de `C2S_Input`
  no servidor e a `SoloSession` fazem.
- `World::request_move_to(id, tile)` planeja com `sim::Pathfinder` (A*) e guarda um
  `CPathFollow`. Chamado mid-step, planeja a partir do tile que está sendo
  **entrado**, senão o ator dá um passo visível para trás.

`sim::update_path_followers(world)` **tem que rodar todo tick, depois de
`world.step()`**. Se rodar antes, o passo só é emitido no tick seguinte ao término
do anterior e andar por rota fica visivelmente mais lento que segurar a tecla.

Custos do A* são em **ticks**, não em tiles (`kPathCostCardinal`/`kPathCostDiagonal`),
para a rota escolhida ser a mais rápida e não a de menos tiles.

Rotas são planejadas **ignorando outros atores**: eles se movem, então desviar deles
gera rota velha. Bloqueio transitório é tratado no momento do passo, e o seguidor
desiste após `kPathBlockedGiveUpTicks`.

### Movimento por tile é o contrato do netcode

Atores não têm posição contínua. Têm `CPosition::tile` (inteiro) e, quando se
movem, um `CWalk { from, to, start_tick, end_tick }`. A ausência de `CWalk` **é** o
significado de "parado".

O deslize suave na tela é o cliente interpolando via `sim::interpolate()`. Um
`ActorState` na rede carrega o tile de **origem** + direção + progresso 0..255, não
uma posição interpolada — por isso um snapshot perdido não custa nada e snapshots
podem ir a 10 Hz com simulação a 30 Hz.

Ao mexer em movimento: `World::request_walk` reserva o tile de **destino** no
início do passo e libera o de origem imediatamente, então dois atores nunca miram o
mesmo tile. Diagonais exigem que **ambos** os ortogonais adjacentes sejam
caminháveis (regra estrita, sem cortar quina). Um passo recusado ainda **gira** o
ator.

### Coordenadas isométricas

`src/client/include/client/iso.hpp` é a única fonte da projeção. O ponto de
referência de um tile é o **vértice de cima**, não o canto do bounding box — a
fórmula é literal por causa disso.

Todo sprite é posicionado por `AtlasEntry::origin_x/origin_y`, que é o deslocamento
daquele vértice até o canto superior-esquerdo do sprite. Ao adicionar arte, é esse
offset que você calcula, não uma posição. Exemplos em `tileset.cpp`: chão `(-32, 0)`,
bloco 64×64 `(-32, -32)`, ator 32×48 `(-16, -32)` para os pés caírem no centro do
tile.

Ordem de desenho é painter's algorithm via `iso::depth_key`: andar domina, e dentro
de um andar o **chão é uma camada plana embaixo de tudo** — só objetos e atores
ordenam por `tile_x + tile_y` e depois por camada.

Chão não ordena por posição de propósito. Um tile de piso é plano e nunca pode estar
legitimamente na frente de algo num tile vizinho; quando ordenava por posição, o chão
do tile que o ator estava atravessando ficava por cima dele e a borda do losango
cortava o sprite durante todo o passo. `tests/test_iso.cpp` tem o teste de regressão.

### Direções: grid ≠ tela

`sim::Direction` é em **espaço de grid** (`North` = `(0,-1)`). Na projeção 2:1, grid
`NorthWest` é o que aparece como **cima** na tela. A rotação vive só em
`client::input::to_grid()`; `sim/` nunca sabe de tela. `tests/test_input.cpp` valida
isso contra a própria projeção, não contra uma tabela.

### Sessão: solo e remoto são a mesma coisa a jusante

`client::Session` entrega um `WorldView` (um `TileMap` + `vector<ActorState>`). Não
há `entt::registry` nem `World` nele.

`SoloSession` roda o `World` de verdade e monta o mesmo `Snapshot` que o servidor
mandaria, **de propósito** — se ler o registry direto, o single-player ganha
comportamento que o servidor não valida e ninguém descobre até testar em rede.

### Rede

`net::ITransport` esconde o ENet (troca por GameNetworkingSockets é uma segunda
implementação, nada de código de jogo muda). Canal confiável para login/chunks de
mapa/chat; não confiável sequenciado para snapshots, e o cliente descarta snapshot
com tick menor que o último visto.

O servidor só conta a cada jogador sobre atores dentro de 23×17 tiles
(`sim::kAoiHalfX/Y`) no mesmo andar, e transmite o mapa em chunks de 16×16 lembrando
o que já mandou por conexão. Chunks totalmente vazios são marcados como enviados
**sem transmitir**.

### Renderer

`client::Renderer2D` não tem tipo de SDL no header. A implementação é
`SDL_Render` + `SDL_RenderGeometry`, com sort por profundidade e batch por textura —
com um atlas só, a cena inteira sai em 1 draw call (confira com
`last_draw_calls()`). `SDL_Render` **não aceita shader customizado**; o dia que
precisar de luz/palette swap/outline, escreva um backend SDL_GPU atrás da mesma
interface.

## Invariantes que quebram em silêncio

- **Mudou o formato de fio?** Bump em `net::kProtocolVersion` (hoje 6). O servidor
  rejeita cliente com versão diferente no Hello, o que é a única coisa que impede um
  misparse silencioso. O Hello também carrega `sim::content_hash` do catálogo de
  itens, conferido do mesmo jeito: servidor e cliente leem conteúdo de fontes
  diferentes (banco vs blob), e sem isso um item editado sem rebake muda em silêncio
  o que bloqueia e quanto ataca. `net::ITransport::disconnect` usa
  `enet_peer_disconnect_later` de propósito, senão o disconnect corre com o pacote de
  reject e o cliente só vê "disconnected" sem o motivo.
- **`sim::can_traverse` é a única regra de geometria de passo.** Tanto
  `World::can_enter` quanto o `Pathfinder` a chamam. Se as duas usarem regras
  diferentes, o A* devolve rotas que o movimento recusa e o ator trava para sempre
  num passo que a própria rota mandou dar. Mexeu na regra de quina? Mexeu num lugar
  só.
- **`core::BitWriter`/`BitReader` têm overflow grudento.** Escritas além do fim são
  descartadas e `overflowed()` trava em true; leituras além do fim devolvem 0 e
  travam. Confira **uma vez no fim**, não campo a campo. Todo `read_*` de protocolo
  retorna `false` nesse caso e o output não pode ser usado. Moram em `core/`, não em
  `net/`, porque o blob de conteúdo (`sim/content_blob.hpp`) usa os mesmos
  primitivos e `sim/` não pode incluir `net/`.
- **Não use `<random>` em `sim/`.** As distribuições da stdlib não são
  especificadas para dar a mesma sequência entre implementações, e mapa por seed
  precisa ser igual em Linux e Windows. Use `sim::Rng`.
- **`core::log_write` sempre dá flush.** Sem isso, `stdout` do servidor redirecionado
  fica full-buffered e um kill por sinal perde todo o log. `setvbuf(_IOLBF)` não
  serve: no Windows equivale a full buffering.
- **Dependências são pinadas por SHA, nunca por tag,** em `cmake/Dependencies.cmake`,
  e declaradas `SYSTEM` para que os avisos delas não derrubem o nosso `-Werror`.
  Cache em `.deps/` na raiz (não em `build/`) para ser compartilhável e copiável
  para máquina offline. Ao trocar versão, edite o SHA **e** o comentário juntos e
  rode `verify-deps.sh`.
- **`entt::null` em macro do doctest precisa de parênteses extras:**
  `CHECK((entity != entt::null))`. Sem isso, a decomposição de expressão do doctest
  fica ambígua contra os operadores do `entt::null_t`.
- **`main.cpp` parseia argumentos duas vezes** de propósito: antes do SDL (para
  `--help` funcionar sem display) e depois do `client.cfg` (para linha de comando
  ganhar do arquivo).
- **Id de item é contrato.** Uma vez shippado, o significado de um id não muda e o
  número nunca é reciclado: mapa salvo e cliente antigo referenciam item por número,
  então reemitir o 1723 transforma um mapa antigo em outra coisa sem ninguém notar.
  `retired_item_ids` guarda os aposentados e `next_free_item_id` tira o máximo das
  **duas** tabelas. Aposentar é `shift+del` no editor, não `DELETE`.
- **Salvar jogador ANTES do despawn.** No handler de desconexão, `persist()` vem
  antes de `world.despawn()`: depois dele não existe mais ator de onde ler posição, e
  o save gravaria um personagem default por cima de um real. Mesma razão pela qual
  `snapshot_character` devolve `nullopt` em vez de um `CharacterSave{}`.
- **Assets só por `platform::vfs`.** No Android eles ficam dentro do APK e não são
  arquivos; `<fstream>` funciona no desktop e falha no device, meses depois.

## Verificação visual sem display

```bash
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_client --solo --seed 23 --zoom 1 \
  --screenshot /tmp/shot.bmp --screenshot-frame 30
```

Grava um BMP e sai. É como conferir mudança de render nesta máquina, e como pedir
para alguém mandar exatamente o que está vendo.

O `game_editor` aceita o mesmo `--screenshot`, e loga o número de draw calls junto —
headless é o único lugar onde isso é observável (o cliente põe no título da janela).
Um atlas só, cena inteira em **1 draw call**, texto incluído; se subir, o batch
quebrou.

```bash
# O modo item e o seletor de sprite, sem janela: o driver dummy não entrega teclas,
# então estas flags existem para alcançá-los.
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --item-mode      --screenshot /tmp/f.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --sprite-picker  --screenshot /tmp/p.bmp

# Vincular sprite sem clicar — kind:id:coluna:linha, em células. Também serve para
# vincular em lote quando chega uma folha de arte nova.
./build/debug/bin/game_editor --bind-sprite object:103:3:1
```

**O que isso NÃO cobre:** digitar no formulário. O driver dummy não entrega teclas,
então o caminho teclado→banco nunca foi exercitado automaticamente — os caminhos de
banco têm teste unitário e o `--bind-sprite` chama a mesma função que o clique, mas o
input em si é território não testado. Ver `docs/pendencias.md`.

## Limites conhecidos — não são bugs para consertar de passagem

`docs/roadmap.md` tem a tabela completa ("Coisas que estão erradas de propósito").
Os que mais confundem:

- **Não existe autenticação.** O `HelloMsg` carrega um nome e nenhuma credencial, então
  o nome **é** a identidade: qualquer um digita o nome de outro e recebe o personagem
  dele. Aceitável em rede confiável, inaceitável fora dela. Foi deixado de fora de
  propósito (precisa de KDF de verdade, decisão de produto e mudança de protocolo) —
  ver `docs/pendencias.md`.
- **Um personagem por conta.** O schema permite vários; a query não.
- **Andares 1 e 2 são inalcançáveis**: não existe escada nem rampa, e o spawn é
  sempre `z=0`. Toda a infra multi-andar existe e é testada, mas nunca foi vista na
  tela.
- `iso::depth_key` ordena errado objeto maior que 1 tile (precisa de sort
  topológico).
- `write_snapshot` trunca em 255 atores sem critério (falta prioridade por ator).
- Rotas não são replanejadas quando outro ator bloqueia: o seguidor espera e
  desiste.
- Sem client-side prediction; a latência visível é o início de um passo.
- `sim::update_wanderers` é placeholder explícito e deve ser deletado quando
  houver IA.

Nada disso está escondido: cada um tem comentário no ponto do código onde morde.
