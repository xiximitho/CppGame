# Mapas: formato de texto e carregamento

Mapas autorados vivem em arquivos de texto (`assets/maps/*.txt`), editáveis à mão
hoje e por um editor simples depois. Isto é o caminho "mapa autorado" do
[content.md](content.md); coexiste com o gerador procedural por seed.

## Onde o parsing mora (e por que)

`sim::parse_text_map` (`sim/map_io.hpp`) faz **só** texto → `TileMap`. É puro: sem
I/O, sem SDL, sem relógio — as regras de camada do `sim/`. Quem lê o arquivo é a
borda, e difere por plataforma de propósito:

- **Cliente** (`SoloSession`): `platform::vfs::read_asset_text`, porque no Android
  o mapa está dentro do APK e `<fstream>` não o abre.
- **Servidor** (`main.cpp`): `<fstream>`, porque o servidor não linka SDL (preset
  `server-only`) e nunca roda em Android.

Os dois chamam o **mesmo** parser, então formato e semântica nunca divergem — a
invariante solo≡servidor. Ambos caem no mapa gerado se o arquivo faltar ou não
parsear, então um clone limpo continua rodando.

O `blocking` de cada tile é **derivado do `ItemTypeRegistry`** (flag `BlocksWalk`),
igual ao gerador — o arquivo de mapa não repete "isto bloqueia". Água e parede
bloqueiam porque seus `ItemType` dizem isso.

## Formato

Baseado em linhas. `#` na coluna 0 é comentário; linhas em branco são ignoradas
(exceto dentro de uma grade, lida literalmente).

```
size <w> <h> <floors>
legend <char> <ground_id> [object_id]   # liga um caractere da grade a tiles
spawn <char>                             # marca esse caractere como spawn
monster <x> <y> <z> <classe>             # um mob que não volta
spawner <x> <y> <z> <classe> <n> <raio> <seg>   # uma população que repõe
portal <x> <y> <z> <dx> <dy> <dz>        # warp: pisa em x,y,z e cai em dx,dy,dz
floor <z>                                # as próximas <h> linhas são a grade
<h linhas de até w caracteres>
```

- `legend`: `ground_id`/`object_id` são `sim::TileId` (os mesmos de
  `tile_ids.hpp`). `object_id` é opcional; ausente = sem objeto.
- `spawn`: opcional, e **usado**: servidor e solo nascem o jogador ali, caindo em
  `find_spawn_tile` quando o tile está ocupado.
- `monster` e `spawner`: opcionais, repetíveis. Ver "Mobs autorados" abaixo e
  [monsters.md](monsters.md).
- `portal`: opcional, repetível. Ver "Portais (warp)" abaixo.
- Um **espaço** (ou uma linha mais curta que `w`) é **vazio**: sem chão, um buraco
  não-caminhável — como se desenha a área escura fora do calabouço.
- Caractere não declarado na legenda é erro (pega typo). `parse_text_map` devolve
  `std::nullopt` e uma razão legível.

Exemplo mínimo:

```
size 5 3 1
legend . 3          # piso de pedra
legend # 3 100      # pedra + parede
legend ~ 4          # água
legend @ 3
spawn @
floor 0
#####
#.@~#
#####
```

## Os mapas que existem

| Arquivo | Tamanho | O que é | Portal |
|---|---|---|---|
| `dungeon.txt` | 56×40×1 | calabouço de pedra: salas e corredores escavados | (34,16) ↔ (39,34) |
| `floresta.txt` | 64×48×1 | grama, estrada de terra, riacho com **vau**, acampamento | (44,17) ↔ (4,43) |
| `vila.txt` | 48×40×**2** | recinto murado com portão, casas, poço, feira, horta, e uma sala no andar 1 | (20,33,0) ↔ (14,19,**1**) |
| `caverna.txt` | 56×40×1 | galerias orgânicas (autômato celular), lago, vazio ao redor | (26,24) ↔ (3,2) |
| `ilha.txt` | 48×48×1 | ilha cercada de água, praia, ruína de pedra, banco de areia | (18,40) ↔ (44,18) |
| `torre.txt` | 40×40×**3** | pátio no andar 0 e dois andares de torre acima, ligados por escada | (18,32,0) ↔ (22,14,**2**) |

Todo mapa tem **um** par de portal, nos dois sentidos (duas linhas `portal`), e os
dois de vários andares atravessam andar — ver "Portais (warp)" abaixo.

`dungeon.txt` vem de `tools/gen_dungeon.py`; os outros cinco de
`tools/gen_maps.py`. Os dois são determinísticos por seed.

⚠️ **Dois mapas da árvore NÃO são mais a saída do gerador**, e rodar o gerador por
cima deles descarta trabalho de verdade:

- **`dungeon.txt`** foi editado no `game_editor` (259 tiles diferem em
  caminhabilidade) e os ninhos foram colocados na geometria editada.
- **`vila.txt`** também: ela ganhou um **segundo andar com escadas** no editor, e o
  `gen_vila` continua gerando um mapa de um andar. Descoberto do jeito ruim — rodei
  `gen_maps.py` sem conferir e a versão editada foi por cima; estava commitada, então
  nada foi perdido, e o `git status` depois de gerar é o anteparo.

Os outros quatro de `gen_maps.py` continuam reproduzíveis byte a byte:

```bash
python3 tools/gen_dungeon.py assets/maps/dungeon.txt   # descarta a edição!
python3 tools/gen_maps.py floresta caverna ilha torre   # esses, sim
python3 tools/gen_maps.py                 # inclui vila: descarta a edição!
```

### Por que gerador e não grade digitada à mão

Tile que bloqueia é fácil de colocar num lugar que **sela um corredor** ou
**ilha um bolsão**, e nem o parser nem um screenshot notam: o mapa carrega, fica
bonito, e uma parte dele é inalcançável para sempre. Então em `gen_maps.py` toda
colocação de tile bloqueante passa por `place_guarded`, que a desfaz se ela
encolher a região alcançável a partir do spawn; `seal_pockets` transforma o que
sobrou de inalcançável em cenário; e `validate` reconfere a grade pronta,
falhando com exit code 1. O flood fill respeita a **mesma** regra de quina que
`sim::can_traverse` (diagonal exige os dois ortogonais), senão ele reivindicaria
uma passagem que o movimento recusa.

Resultado: em todo mapa commitado a área caminhável é uma única região conectada,
**atravessando andares por escada**. É por isso que o spawn aleatório de
`find_spawn_tile` é inofensivo — qualquer tile caminhável alcança todos os outros.

### Mobs autorados

```
monster <x> <y> <z> <classe>
```

Uma linha por mob, em qualquer lugar do arquivo depois do `size`. A classe é o id de
`sim::monsters::` (1 rato, 2 esqueleto, 3 ogro) e **id de classe é contrato**, igual
a id de item. `parse_text_map` devolve as linhas em `ParsedMap::monsters`; quem
spawna é `sim::spawn_authored_monsters`, que **pula** o que não dá para colocar
(classe inexistente, tile não caminhável, tile ocupado) em vez de derrubar o
servidor por causa de conteúdo.

Walkability **não** é checada no parser de propósito: uma linha `monster` pode vir
antes da grade que preenche aquele tile. Quem checa é o `gen_maps.py`, na hora de
autorar, onde ainda dá para consertar — e o `validate` também reprova mob em tile
inalcançável, que é um mob que ninguém encontra.

Para **população** use `spawner` em vez de repetir `monster`:

```
spawner <x> <y> <z> <classe> <quantos> <raio> <segundos>
```

Mantém `<quantos>` vivos no raio e repõe um por vez, com o relógio começando na
morte. Detalhes em [monsters.md](monsters.md). O `validate` do gerador reprova ninho
em rocha, ninho inalcançável e população que não cabe no raio.

O `spawn @` do arquivo agora **é usado** (servidor e solo) — mas só como
**preferência**: se o tile não for caminhável ou estiver ocupado, cai em
`find_spawn_tile` com `LOG_WARN`. Isso não é teoria: o `@` do `dungeon.txt` está num
tile de água+parede (alguém pintou por cima dele no editor), e confiar nele colocaria
todo jogador novo dentro da rocha.

### Escadas

`<` sobe um andar, `>` desce (ids 103/104, flags `StairsUp`/`StairsDown`). A regra
mora em `sim::World::apply_tile_transition` (a mesma do portal) e dispara quando um
passo **termina** naquele tile: o ator vai para o mesmo x,y no andar de destino, se
lá for caminhável e estiver livre. Um par simétrico (`<` embaixo, `>` no mesmo x,y em cima) é o que
permite voltar, e não fica em loop porque ser *colocado* numa escada não conta como
passo.

O `reachable3` do gerador atravessa escada com exatamente essa semântica — incluindo
o detalhe de que quem chegou por escada pode sair andando — e `validate` reprova
escada que dá em rocha ("leads nowhere"). Foi assim que os três andares da
`torre.txt` passaram a ser verificados de verdade e não "por design inalcançáveis".

**Escada que não parece funcionar é quase sempre escada sem destino.** O
`apply_tile_transition` recusa em silêncio quando o andar de destino não existe
(mapa de um andar só), é rocha, ou está ocupado — e recusar é igualzinho a "não funciona" para
quem está pisando nela. Dois anteparos contra isso: o editor avisa na barra de baixo
quando a escada na mão não tem para onde levar (e `Ctrl+PgUp` adiciona o andar), e
`tests/test_shipped_maps.cpp` reprova qualquer escada dos mapas commitados que dê num
tile onde não se pode ficar de pé. Dos seis mapas, **só a `torre.txt` tem escada** —
os outros cinco são de um andar, então não há escada para funcionar neles.

O `.txt` continua editável à mão e no `game_editor` depois de gerado; se você for
editar, edite o `.txt` e ignore o gerador (ou o próximo `gen_maps.py` passa por
cima).

### Portais (warp)

```
portal <x> <y> <z> <dx> <dy> <dz>
```

Pisar em `x,y,z` põe o ator em `dx,dy,dz`, em qualquer lugar do mapa — o warp do
Tibia. Uma linha por portal, repetível, em qualquer lugar depois do `size`.

**Por que o destino está no mapa e não no tipo de item.** Escada é *relativa*
(`z±1`), então cabe numa flag do tipo e o autor só pinta o tile. Warp é *absoluto*, e
id de item é contrato compartilhado: dois portais com destinos diferentes usam o mesmo
sprite. Então o par de coordenadas é dado do **tile**, e mora aqui.

Ida e volta são **duas** linhas — uma em cada ponta. É o jeito normal de autorar, e é
por isso que a regra "dispara na chegada por passo, nunca na chegada por transição" é
essencial e não cosmética: sem ela, todo par bidirecional teleportaria seu usuário para
sempre. Mesmo ponto do código onde a escada age (`World::apply_tile_transition`), com
as mesmas recusas em silêncio (destino em rocha, ocupado, fora do mapa).

**Linha errada é erro de parse, não portal morto.** Ao contrário de `monster` — que o
spawner pula quando não dá para colocar —, um `portal` com ponta fora do mapa, com
destino igual à origem, ou com qualquer das duas pontas num tile onde não se pode ficar
de pé **recusa o mapa inteiro**, com razão legível. A diferença é quem digitou as
coordenadas: destino de warp é número escrito à mão, e o runtime recusa em silêncio, o
que seria indistinguível de "o portal não funciona". Walkability é checada no **fim** do
parse, então a linha `portal` pode vir antes da grade que preenche aqueles tiles.

#### O marcador: glifo `P`, item 105

Um `portal` sozinho é um tile **invisível** que teleporta quem pisa. Então a boca tem
arte: item **105** (`sim::tiles::kPortal`), com a flag `Teleport`, glifo `P` na
legenda (`legend P 3 105`), desenhado como um golfo violeta com espiral ciano pelo
`tools/gen_placeholder_atlas.py --patch` — de propósito diferente do poço da escada,
que é o mesmo losango em cinza.

A flag **não é lida por nenhuma regra**: destino é dado do tile, não do tipo. Ela
existe para o portal ser visível e para o cruzamento poder ser checado, e
`tests/test_shipped_maps.cpp` reprova as duas derivas, que são silenciosas em jogo e
invisíveis num screenshot:

- linha `portal` sem marcador → warp vivo e invisível;
- marcador sem linha `portal` → portal pintado que não faz nada.

#### Como os portais dos seis mapas foram escolhidos

Uma regra só, em vez de uma tabela de coordenadas por mapa: **a boca de perto é o
tile aberto mais próximo do spawn a pelo menos 5 tiles, e a de longe é o tile aberto
mais distante dele no andar de cima**. É "o caminho longo de volta", que é para o que
um warp serve — e tirar a ponta distante do último andar dá de graça um warp que muda
`z` na `torre` e na `vila`, o caso que um par de escadas não expressa.

"Aberto" quer dizer **os quatro ortogonais caminháveis**. Portal em corredor de 1 tile
de largura **muraria** o corredor: pisar na boca manda você para longe, então o que
estava depois dela não é mais alcançável por ali. O `reachable3` do gerador modela
isso (portal é **aresta** no flood fill, checado antes da escada, igual à regra), e a
escolha de tile aberto evita a armadilha na origem.

- Nos quatro mapas reproduzíveis: `tools/gen_maps.py` faz tudo (glifo, linhas e
  validação).
- Em `dungeon.txt` e `vila.txt`, que o gerador não pode mais produzir:
  `python3 tools/add_portal.py assets/maps/dungeon.txt` aplica a **mesma** regra
  lendo o `.txt` pronto. É idempotente: arquivo que já tem `portal` fica intocado.

O `add_portal.py` topou com dois dentes do `dungeon.txt`: o `@` dele está em
água+parede (o wart já conhecido), então não há spawn de onde medir — ele cai para o
meio da **maior região conectada**, e "maior" não é detalhe: o tile mais próximo do
centro do mapa é um bolsão de **um** tile, e ancorar ali daria um portal que ninguém
alcança.

**O editor ainda não autora portal.** As linhas são preservadas num save
(`ParsedMap::portals` → writer), mas não há UI para criar ou mover — ver
[pendencias.md](pendencias.md), fase T3.

## Escolher qual mapa carregar

```bash
./build/debug/bin/game_client --solo --map maps/ilha.txt     # relativo aos assets
./build/debug/bin/game_client --solo --map ilha              # o nome também serve
./build/debug/bin/game_server --map assets/maps/vila.txt     # relativo ao CWD
./build/debug/bin/game_editor --map vila                     # ou qualquer caminho
```

Cliente e editor aceitam a forma curta: um nome (`ilha`), um caminho relativo aos
assets (`maps/ilha.txt`), ou o caminho completo que o editor imprime no log — cada
um tenta os candidatos em ordem e loga qual resolveu. Isso existe porque a
alternativa era um typo silenciosamente virar "mapa procedural" (cliente) ou "tela
em branco" (editor), o que parece a flag ter sido ignorada. O editor ainda aceita um
nome que **não** existe: é um mapa novo, gravado no `S`.

Por baixo os caminhos ainda diferem, porque as bordas diferem: o cliente resolve
pelo `platform::vfs` (no Android o mapa está dentro do pacote), o servidor e o editor
abrem um arquivo comum. Sem a flag, o padrão de cada um continua sendo o calabouço.
Um caminho ilegível ou que não parseia cai no mapa procedural por seed, com
`LOG_WARN` dizendo o motivo — um clone sem os assets continua rodando.

O cliente também aceita `map=maps/ilha.txt` no `client.cfg`; a linha de comando
ganha do arquivo.

Em rede, quem manda é o servidor: o mapa do cliente é irrelevante, ele recebe o
do servidor em chunks. `--map` no cliente vale para `--solo`. No `run-local.sh` é o
servidor que precisa da flag, então existe `GAME_MAP`:

```bash
GAME_MAP=torre ./scripts/run-local.sh          # o único mapa com escada
```

## O editor (`game_editor`)

Um editor isométrico simples que reusa o renderer e o tileset do cliente.
Carrega um mapa texto, desenha, e deixa pintar tiles com o mouse; a paleta é
montada em runtime a partir do `ItemTypeRegistry` e dos sprites presentes no
atlas — ou seja, mostra exatamente os ids de objeto que dá para colocar. Salvar
escreve o mapa de volta com `sim::write_text_map`.

```bash
./build/debug/bin/game_editor --map assets/maps/dungeon.txt   # rode da raiz do repo
```

Uma **barra de paleta** na base mostra todos os brushes (os ids do catálogo que
têm sprite, mais "apagar objeto" e "vazio"); o selecionado ganha moldura dourada.
Clique numa célula para escolher, ou use o teclado.

Controles:

| Entrada | Ação |
|---|---|
| clique/arrasto esquerdo | coloca o brush atual |
| clique/arrasto direito | apaga o objeto do tile |
| clique na barra da paleta | escolhe o brush |
| `Tab` / `]` / `[` | próximo / anterior brush |
| `0`–`9` | escolhe brush pelo índice |
| `Ctrl+Z` / `Ctrl+Y` | desfaz / refaz (um passo por traço) |
| `PgUp` / `PgDn` | sobe / desce o andar editado |
| `Ctrl+PgUp` | adiciona um andar vazio em cima |
| `F3` | abre a lista de mapas do diretório atual |
| setas | pan · roda do mouse ou `+`/`-` | zoom |
| `S` | salvar · `Esc` | sair |

Um "fantasma" do brush é desenhado sob o cursor, então dá para ver o que o clique
vai colocar. Sem arquivo em `--map`, ele começa numa tela de pedra 48×32 em
branco. Salvar só acontece no `S` — o editor nunca escreve o arquivo sozinho.

**Andares.** O andar editado aparece na barra de baixo e no título (`floor 1/2`);
os andares abaixo continuam desenhados, escurecidos, e os de cima não — a ideia é
enxergar o que está sob o pincel, não a laje em cima dele. Trocar de andar move a
câmera junto (um andar é `iso::kFloorHeight` acima na tela), senão o mapa parece
fugir do cursor. `Ctrl+PgUp` cresce o `TileMap` (que não tem resize: é uma grade
densa dimensionada uma vez para um mundo rodando, então crescer é reconstruir —
tranquilo na autoria, impensável no tick).

**`F3` — abrir outro mapa.** Lista os `.txt` do diretório do mapa atual
(`src/editor/map_browser.hpp`); setas escolhem, `Enter` abre, `Esc` cancela. Com
alteração não salva, o primeiro `Enter` só arma o aviso e o segundo confirma. Abrir
limpa o histórico de undo — um undo cujos snapshots são de outro arquivo é pior que
nenhum.

Verificação sem display (como o cliente). O driver dummy não entrega teclas, então
as flags existem para alcançar o que normalmente é teclado:

```bash
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor \
  --map dungeon --screenshot /tmp/ed.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor \
  --map torre --floor 1 --brush 7 --screenshot /tmp/ed.bmp   # andar 1, brush escada
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor \
  --map-browser --screenshot /tmp/ed.bmp                     # a lista de mapas
```

### Limites atuais / próximos passos

- **Nomes em vez de ids na legenda** — precisa de uma tabela nome→id (hoje
  `build_default_registry` não guarda nomes; nomes são apresentação, cliente). O
  editor já rotula com nomes conhecidos, mas o arquivo salva ids numéricos.
- **Escolher/mover o `spawn`** pelo editor (hoje ele preserva o spawn carregado,
  mas não deixa recolocá-lo).
- **Colocar mob pelo editor.** O `monster` do arquivo é preservado num save (o
  editor guarda o que carregou e o writer regrava), mas não há UI para adicionar,
  mover ou trocar a classe de um. Hoje é editar o `.txt` ou o `gen_maps.py`.
- **Multi-andar no editor**: resolvido. `PgUp`/`PgDn` trocam o andar editado,
  `Ctrl+PgUp` adiciona um andar em cima, e o andar de baixo continua desenhado
  (escurecido) para dar referência. Com a escada na mão, o editor avisa quando o
  andar de destino não existe — o caso que a `apply_tile_transition` recusa em silêncio.
  O que o save ainda perde são os comentários do arquivo: o writer regera o
  cabeçalho e a legenda. Há teste garantindo que salvar um mapa shippado não muda
  nenhum tile, andar nem spawner (`tests/test_shipped_maps.cpp`).
- **Caminho de asset do servidor** robusto (hoje é relativo ao CWD, com fallback).
- Eventualmente, assar o `.txt` para o blob binário do content.md.
