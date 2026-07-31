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
floor <z>                                # as próximas <h> linhas são a grade
<h linhas de até w caracteres>
```

- `legend`: `ground_id`/`object_id` são `sim::TileId` (os mesmos de
  `tile_ids.hpp`). `object_id` é opcional; ausente = sem objeto.
- `spawn`: opcional, e **usado**: servidor e solo nascem o jogador ali, caindo em
  `find_spawn_tile` quando o tile está ocupado.
- `monster` e `spawner`: opcionais, repetíveis. Ver "Mobs autorados" abaixo e
  [monsters.md](monsters.md).
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

| Arquivo | Tamanho | O que é |
|---|---|---|
| `dungeon.txt` | 56×40×1 | calabouço de pedra: salas e corredores escavados |
| `floresta.txt` | 64×48×1 | grama, estrada de terra, riacho com **vau**, acampamento |
| `vila.txt` | 48×40×1 | recinto murado com portão, casas, poço, feira, horta |
| `caverna.txt` | 56×40×1 | galerias orgânicas (autômato celular), lago, vazio ao redor |
| `ilha.txt` | 48×48×1 | ilha cercada de água, praia, ruína de pedra, banco de areia |
| `torre.txt` | 40×40×**3** | pátio no andar 0 e dois andares de torre acima, ligados por escada |

`dungeon.txt` vem de `tools/gen_dungeon.py`; os outros cinco de
`tools/gen_maps.py`. Os dois são determinísticos por seed.

⚠️ **O `dungeon.txt` da árvore NÃO é mais a saída do gerador** — ele foi editado no
`game_editor` depois (259 tiles diferem em caminhabilidade) e os ninhos dele foram
colocados na geometria editada. Rodar `gen_dungeon.py` por cima **descarta essa
edição**. Os cinco de `gen_maps.py` continuam reproduzíveis:

```bash
python3 tools/gen_dungeon.py assets/maps/dungeon.txt
python3 tools/gen_maps.py                 # os cinco
python3 tools/gen_maps.py vila caverna    # só esses
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
mora em `sim::World::apply_stairs` e dispara quando um passo **termina** naquele
tile: o ator vai para o mesmo x,y no andar de destino, se lá for caminhável e
estiver livre. Um par simétrico (`<` embaixo, `>` no mesmo x,y em cima) é o que
permite voltar, e não fica em loop porque ser *colocado* numa escada não conta como
passo.

O `reachable3` do gerador atravessa escada com exatamente essa semântica — incluindo
o detalhe de que quem chegou por escada pode sair andando — e `validate` reprova
escada que dá em rocha ("leads nowhere"). Foi assim que os três andares da
`torre.txt` passaram a ser verificados de verdade e não "por design inalcançáveis".

O `.txt` continua editável à mão e no `game_editor` depois de gerado; se você for
editar, edite o `.txt` e ignore o gerador (ou o próximo `gen_maps.py` passa por
cima).

## Escolher qual mapa carregar

```bash
./build/debug/bin/game_client --solo --map maps/ilha.txt     # relativo aos assets
./build/debug/bin/game_server --map assets/maps/vila.txt     # relativo ao CWD
./build/debug/bin/game_editor --map assets/maps/vila.txt
```

Os caminhos diferem porque as bordas diferem: o cliente resolve pelo
`platform::vfs` (no Android o mapa está dentro do pacote), o servidor abre um
arquivo comum. Sem a flag, o padrão de cada um continua sendo o calabouço. Um
caminho ilegível ou que não parseia cai no mapa procedural por seed, com
`LOG_WARN` dizendo o motivo — um clone sem os assets continua rodando.

O cliente também aceita `map=maps/ilha.txt` no `client.cfg`; a linha de comando
ganha do arquivo.

Em rede, quem manda é o servidor: o mapa do cliente é irrelevante, ele recebe o
do servidor em chunks. `--map` no cliente vale para `--solo`.

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
| setas | pan · roda do mouse ou `+`/`-` | zoom |
| `S` | salvar · `Esc` | sair |

Um "fantasma" do brush é desenhado sob o cursor, então dá para ver o que o clique
vai colocar. Sem arquivo em `--map`, ele começa numa tela de pedra 48×32 em
branco. Salvar só acontece no `S` — o editor nunca escreve o arquivo sozinho.

Verificação sem display (como o cliente):

```bash
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor \
  --map assets/maps/dungeon.txt --screenshot /tmp/ed.bmp
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
- **Multi-andar**: o editor mostra e edita só o andar 0, então a escada dá para
  pintar mas o andar de destino não (o formato, o parser e o writer suportam
  vários — `torre.txt` tem três). Ele guarda o `TileMap` inteiro e
  `write_text_map` escreve todos os andares, então salvar `torre.txt` **não perde**
  os andares 1 e 2; falta UI para trocar de andar. O que o save perde são os
  comentários do arquivo: o writer regera o cabeçalho e a legenda.
- **Caminho de asset do servidor** robusto (hoje é relativo ao CWD, com fallback).
- Eventualmente, assar o `.txt` para o blob binário do content.md.
