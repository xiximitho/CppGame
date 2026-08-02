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
./build/debug/bin/game_client --solo --map maps/ilha.txt   # outro mapa autorado
# balancear mob: editar assets/monsters.txt e rodar (sem build, sem rebake)
./scripts/run-local.sh                                 # sobe servidor + cliente
./build/debug/bin/game_client --connect 127.0.0.1:7777 --name felipe

# Autorar conteúdo — F2 alterna mapa <-> modo item, F3 escolhe o mapa,
# F4 alterna mapa <-> modo mob (animação de uma classe),
# PgUp/PgDn trocam de andar, Ctrl+PgUp adiciona um andar
./build/debug/bin/game_editor --map torre              # nome, caminho ou nada
./build/debug/bin/game_bake                            # content.db -> content.bin

# Trazer arte de mob de uma folha estilo Tibia para o atlas (docs/animation.md)
python3 tools/import_otsp.py --sheet assets/tibia_like/otsp_creatures_03.png \
    --at 0,47 --cell 32x32 --appearance 2

# Portal (warp) num mapa que o gerador não refaz — mesma regra do gen_maps.py
python3 tools/add_portal.py assets/maps/dungeon.txt

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

Mapa também é dado: `assets/maps/*.txt`, escolhido por `--map` no cliente (solo),
no servidor e no editor — no cliente e no editor, `--map torre`, `--map
maps/torre.txt` e o caminho completo todos resolvem, porque a alternativa era um typo
virar "mapa procedural" em silêncio. No editor, `F3` escolhe da lista. Os seis mapas commitados saem de geradores determinísticos
(`tools/gen_dungeon.py`, `tools/gen_maps.py`); o `gen_maps.py` **valida
conectividade** antes de gravar, porque um tile bloqueante mal colocado sela um
corredor sem que parser nem screenshot reclamem — e portal conta como aresta nesse
flood fill, senão "all reachable" mente. Ver `docs/maps.md`.

⚠️ **`dungeon.txt` e `vila.txt` não são mais a saída do gerador** (foram editados no
editor; a `vila` ganhou um segundo andar). Rodar o gerador por cima **descarta a
edição** — para eles, `tools/add_portal.py` e a mão. `git status` depois de gerar é o
anteparo. Os outros quatro reproduzem byte a byte.

## Arquitetura

Documentação de fundo em `docs/architecture.md`, `docs/dependencies.md`,
`docs/mobile.md`, `docs/roadmap.md`. Para conteúdo e autoria: `docs/content.md` (o
pipeline e o porquê das decisões), `docs/authoring.md` (as receitas),
`docs/sprites.md`, `docs/monsters.md` (classes de mob e spawners),
`docs/animation.md` (frames de caminhada, as folhas do pacote OTSP e o modo mob). **`docs/pendencias.md` é o handoff**: o que está pronto, o que
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

O outro lado da mesma moeda é `client::anim::art_direction()`: arte estilo Tibia tem
**4** direções desenhadas em espaço de tela (costas, direita, frente, esquerda), e as
quatro direções de arte **são** as quatro diagonais de grid. Cada cardinal de grid cai
no meio de duas, empate desempatado no horário — duas direções de grid por coluna de
arte. Mesma regra de sempre: a rotação mora no cliente e é testada contra a projeção.

### Animação é o progresso do passo, não um relógio

`client::anim::walk_frame(walking, progress, frames)` (header, `inline`, puro) escolhe
o frame a partir de `ActorState::walk_progress`, que o snapshot **já carrega**. Por isso
animar não mexeu em `sim/`, não mexeu no servidor e não bumpou
`net::kProtocolVersion`: é apresentação. E é por isso que é robusto — a fase é
**derivada** da posição no passo em vez de acumulada num contador local, então snapshot
perdido custa à animação o que custa à posição, ou seja nada.

Um conjunto animado é uma linha do `atlas.txt`: `mobstrip <appearance> <x> <y>
<cell_w> <cell_h> <dirs> <frames> <ox> <oy>`, células numa fileira só,
**direção-maior** (`dir * frames + frame`). O kind antigo `mob <appearance> <dir> ...`
segue valendo e é o que o rato usa — o caminho estático continua exercitado de
propósito. **Os dois não podem coexistir para a mesma aparência**: são lidos na ordem do
arquivo e o último ganha, então o importador e o editor apagam as linhas `mob` da
aparência que ganha `mobstrip`.

Conjunto estático responde `frames == 1`, e `walk_frame` responde 0 para ele — por isso
nenhum caller tem `if` de "não animado".

A linha tem um campo `tilt` **opcional** no fim, em graus, e **hoje nada shippado usa
valor diferente de 0** — de propósito. O pacote OTSP mistura duas convenções, medido
célula por célula:

| Faixa de `otsp_creatures_03.png` | Como está desenhada | O que fazemos |
|---|---|---|
| linhas 47–48 (fantasma, criatura alada) → aparências 2 e 3 | **de pé** | nada, `tilt 0` |
| linhas 1–25, coluna 1 (template humano) → jogador | **deitada a 45°** | **assada em pé no import** |

O `tilt` já foi 30, errado nas duas pontas: inclinava arte que não precisava (o mob
saía do losango) e não endireitava a que precisava (o jogador parava em 75°).

**Por que assar em vez de girar no render.** `SpriteCmd::rotation` gira em torno do
**pé** do sprite, então a figura balança para o lado: a 45°, o pé de um 32×48 sai
+11px em x e +9px em y, e o atlas precisava de um `origin -27 -41` que nada mais no
arquivo usa. Pior, a rotação acontece depois do zoom, então o grid de pixels do sprite
fica torto em relação ao da tela e o contorno vira escadinha. `import_otsp_world.py`
gira as células (`apply_upright`, mapeamento inverso, nearest — o mesmo que a GPU
fazia) e recentra pelo **pé da base**, com o mesmo deslocamento aplicado às 4 máscaras
de roupa, senão a roupa descola. Resultado: `origin` canônico, `tilt 0`, pixel
alinhado, zero conta por sprite.

O campo fica porque é barato e uma folha pode trazer arte tombada — mas a suspeita, ao
ver um sprite desalinhado, começa pela **ancoragem**, não pela rotação.

**As 4 direções são as 4 diagonais, e o par esquerda/direita é fácil de espelhar.**
Nenhuma das quatro poses do template é perfil de lado (largura de ombro 18–21 nas
quatro): duas olham para a câmera, duas para o fundo. Se o boneco andar para uma
diagonal olhando para a oposta, é o `--dir-order`/`dir_order` espelhado — dá para
medir de que lado da cabeça está a pele do rosto, em espaço de tela.

`rotation == 0` desvia do `sin`/`cos`: todo tile de chão passa por ali, e girar não
custa draw call porque o backend já emite quatro vértices por sprite. Arte, medidas
das folhas e receitas em `docs/animation.md`; crédito da arte (CC BY 4.0) em
`assets/tibia_like/CREDITS.md`.

### Ancoragem: o pé do sprite vai no CENTRO do tile, menos quando a arte é o losango

`origin_y = 16 - altura` e `origin_x = -largura/2` é a regra para tudo que **fica de
pé** num tile: ator 32×48 → `(-16,-32)`, rato 24×24 → `(-12,-8)`, tira de mob 32×32 →
`(-16,-16)`. Em todos, a base do sprite cai em `apex + 16`, que é o centro do losango.

A exceção é arte cuja **própria base é o losango do tile** — parede em bloco, escada,
portal: aí `origin_y = -32` faz o losango da arte coincidir com o do tile. Dá para
distinguir medindo as últimas linhas opacas da célula: `24,20,16,12,8,4` afunilando é
base em losango (`-32`); `12,12,12,10,3,2` é um tronco e `52,52,50,50,48,48` é base
chapada de arte de grade quadrada — as duas querem `-48` num sprite de 64×64, senão o
objeto é desenhado 16px baixo demais e derrama no tile da frente.

Isso morde ao importar arte do Tibia: **Tibia não é isométrico** (grade de 32×32
alinhada à tela), então uma caixa importada tem pegada quadrada e nunca casa com o
losango — dá para plantá-la certo, não para fazer a pegada bater sem redesenhar.

### Mobs: classe é dado, comportamento é sistema

`sim::MonsterType` (`sim/monster_type.hpp`) é uma classe de mob: hp, dano, defesa,
alcance, velocidade (`step_ticks`), raio de aggro, leash, sprite (`appearance`) e um
item de loot. As classes vivem em **`assets/monsters.txt`** e são lidas pelo mesmo
parser puro nas duas bordas (servidor por `<fstream>`, cliente por `platform::vfs`);
`sim::default_monsters()` é o **fallback** e tem que concordar com o arquivo. Ajustar
velocidade não recompila nem rebakeia nada — não está no `content.db` porque stat de
monstro só é lido onde a simulação roda, então não há o que o hash de conteúdo
proteja. Receitas e o significado de cada campo em `docs/monsters.md`.
**Id de classe é contrato**, igual a id de item: mapa salvo referencia classe por
número.

Velocidade: `step_ticks` é ticks por passo; o jogador anda a `kDefaultStepTicks` (9)
e **todas** as classes shippadas estão acima disso, então dá para desengajar de
qualquer coisa. Tem teste garantindo isso (`test_spawners.cpp`).

População é autorada no mapa, não aleatória: `spawner <x> <y> <z> <classe> <quantos>
<raio> <segundos>` mantém uma população viva e repõe **um por vez**, com o relógio
começando na morte. `monster <x> <y> <z> <classe>` é o caso oposto: um mob que não
volta. O spawner é entidade **sem `CActor`** (invisível, nunca em snapshot) e guarda
os `NetId` dos filhos em vez de contar mobs por perto. `--wanderers` (0 por padrão)
espalha mobs aleatórios por cima, e existe só para encher o mundo rápido.

`sim::spawn_monster` **copia** os números da classe para componentes do ator
(`CActor::step_ticks`, `CHealth`, `CCombat`, `CMonster`) — por isso nada a jusante
precisa do catálogo em mão, e `sim::combat_stats` só soma `CCombat` + equipamento.
Jogador não tem `CCombat`, então para ele nada mudou.

`sim::update_monsters` decide **quem** atacar: o **não-mob** mais próximo dentro do
aggro (histerese de 1.5x para não ligar/desligar na borda), senão vagueia dentro do
leash em volta de `home`. Quem **anda** é o `update_chasers`, o mesmo sistema que
move o jogador quando ele clica num mob — mob e jogador perseguem pelo mesmo código. **É "não-mob", não "jogador", de
propósito**: no solo o ator local não tem `CPlayer` (não existe peer), então aggro
por `CPlayer` daria comportamento diferente em single-player — exatamente a
divergência que a regra de camadas existe para impedir.

A ordem do tick, nos dois laços (servidor e `SoloSession`), é `step` →
`update_spawners` → `update_monsters` → `update_chasers` → `update_path_followers` →
`update_combat`: decide, planeja, anda, bate — tudo no mesmo tick. Decisões são coletadas e só depois aplicadas: `request_walk` adiciona
componente e invalidaria a view sendo iterada.

Mob morto **não** volta (sem `CRespawn`, ao contrário do jogador) e deixa o loot da
classe no chão.

### A battle list é montada só do `WorldView`

`client::build_battle_list` (header, `inline`, puro) devolve os atores visíveis menos
o local, **mais próximo primeiro**, empate por `net_id` para a linha não trocar de
lugar debaixo do cursor. Distância é Chebyshev, a mesma medida de alcance e aggro —
ordenar de outro jeito discordaria do que está ao alcance do golpe.

Ela sai do `WorldView` e de nada mais, e é isso que dá a semântica do Tibia de
graça: a view é o que o servidor decidiu que aquele jogador pode ver (AoI, andar), 
então o painel não consegue listar o que o jogador não deveria saber. Clique numa
linha é intenção de **ataque** (que implica perseguir), nunca de movimento.

Nome da classe vem do `assets/monsters.txt` lido pelo cliente
(`client::load_monster_catalogue`) — apresentação: em rede os números são do
servidor, então um arquivo local desatualizado mostra nome errado, nunca luta errada.

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
- **Escada e portal agem na chegada POR PASSO, nunca na chegada por transição.**
  `World::step` chama `apply_tile_transition` quando um `CWalk` termina. É isso que
  impede o par simétrico (`<` embaixo, `>` em cima no mesmo x,y) de ficar
  teletransportando o ator para sempre: ser *colocado* na escada de cima não conta
  como passo. Pisar nela de novo, sim. Escada é flag de item
  (`StairsUp`/`StairsDown`), relativa (`z±1`), e recusa em silêncio quando o destino
  é rocha, está ocupado ou **não existe** (mapa de um andar) — não existe
  meio-movimento que deixe occupancy pendurada. `tests/test_stairs.cpp` cobre os
  cinco casos.
  **Portal (warp) é a mesma regra com destino absoluto**, autorado por tile na linha
  `portal <x> <y> <z> <dx> <dy> <dz>` do mapa — não é flag de tipo, porque id de item
  é contrato compartilhado e dois portais têm destinos diferentes com o mesmo sprite.
  Aqui a regra da chegada é ainda mais crítica: ida e volta são duas linhas, uma em
  cada ponta, então o par bidirecional é o caso **normal** e o loop infinito seria o
  padrão. Linha malformada (ponta fora do mapa, destino igual à origem, ponta em
  rocha) **recusa o mapa no parse** em vez de virar portal morto — o oposto de
  `monster`, que o spawner pula. `tests/test_portals.cpp`, e receitas em
  `docs/maps.md`.
  O preço desse silêncio é que "escada quebrada" quase sempre é escada sem destino,
  não regra errada: só a `torre.txt` tem escada, os outros cinco mapas são de um
  andar. Dois anteparos: o editor avisa quando a escada no pincel não tem para onde
  levar, e `tests/test_shipped_maps.cpp` reprova escada dos mapas commitados que dê
  em tile onde não se pode ficar de pé — além de andar da torre até a escada e
  conferir que o andar mudou.
- **Atacar implica perseguir, e perseguir é contínuo.** O cliente manda **quem**,
  nunca **onde**: `C2S_Attack` faz o servidor chamar `set_attack_target` **e**
  `request_follow`, e `sim::update_chasers` replaneja a rota sempre que o alvo se
  move, parando de andar quando ele entra no alcance. O jeito antigo — um
  `request_move_to` para o tile onde o mob estava — é o bug de "segue metade do
  caminho e para de atacar": a rota terminava (ou o último passo era recusado, porque
  o tile do alvo está ocupado) e o ator ficava plantado com um alvo que nunca
  alcançava. Movimento manual (`World::cancel_path`) **cancela a perseguição** —
  senão o chase replaneja no tick seguinte e o input parece ignorado.
- **`set_attack_target` com o MESMO alvo não faz nada.** Ele zerava
  `next_swing_tick` a cada chamada, e o `update_monsters` chama algumas vezes por
  segundo para um mob engajado: mob batia a cada 3 ticks em vez de a cada cooldown,
  ~10x o dano pretendido. `request_follow` tem a mesma guarda, pelo mesmo motivo.
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
# O modo item, o seletor de sprite, a lista de mapas, o andar e o brush, sem janela:
# o driver dummy não entrega teclas, então estas flags existem para alcançá-los.
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --item-mode      --screenshot /tmp/f.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --sprite-picker  --screenshot /tmp/p.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --map-browser    --screenshot /tmp/m.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --mob 2          --screenshot /tmp/b.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --mob 1 --mob-picker --screenshot /tmp/s.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --map torre --floor 1 --brush 7 \
  --screenshot /tmp/z.bmp

# Vincular sprite sem clicar — kind:id:coluna:linha, em células. Também serve para
# vincular em lote quando chega uma folha de arte nova.
./build/debug/bin/game_editor --bind-sprite object:103:3:1
# O mesmo para a tira de animação de um mob:
# appearance:coluna:linha[:dirs:frames:cellw:cellh]
./build/debug/bin/game_editor --bind-mob 2:0:14
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
- **Spawn autorado vale, mas só como preferência**: o `spawn @` do mapa é usado
  pelo servidor e pelo solo; se o tile estiver ocupado (dois logins ao mesmo tempo)
  cai em `find_spawn_tile`. Mapa sem `spawn` (todo mapa gerado por seed) usa
  walkable aleatório, sempre `z=0`.
- `iso::depth_key` ordena errado objeto maior que 1 tile (precisa de sort
  topológico).
- `write_snapshot` trunca em 255 atores sem critério (falta prioridade por ator).
- Rotas não são replanejadas quando outro ator bloqueia: o seguidor espera e
  desiste.
- Sem client-side prediction; a latência visível é o início de um passo.
- O editor não coloca mob, ninho **nem portal**, e não move o spawn (preserva os que
  carregou, mas não tem UI). Andares, sim: `PgUp`/`PgDn` editam qualquer andar e
  `Ctrl+PgUp` adiciona um. Portal se autora no `.txt` ou por `tools/add_portal.py`.
- Loot é **um** item por classe, não tabela com chances. Sem mob ranged usando
  `kind ranged` ainda.

Nada disso está escondido: cada um tem comentário no ponto do código onde morde.
