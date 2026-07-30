# Guia de autoria de conteúdo

Como adicionar **arte, tipos de tile/objeto e mapas** ao jogo. É um cookbook
prático; o "por quê" de fundo está em [content.md](content.md) (a plataforma de
itens), [sprites.md](sprites.md) (como a arte chega na tela) e [maps.md](maps.md)
(formato de mapa e editor).

## Antes de tudo: as três camadas de um tile

Um `TileId` (o número num slot de chão ou objeto do tile) carrega **três
conhecimentos**, cada um numa camada diferente. Ao criar conteúdo você mexe em
uma, duas ou nas três, dependendo do que quer:

| Você quer… | Mexe em | Precisa recompilar? |
|---|---|---|
| **Só trocar a aparência** (redesenhar um sprite existente) | `assets/tilesets/atlas.png` | Não |
| **Vincular um sprite a um id / mudar recorte** | `assets/tilesets/atlas.txt` | Não |
| **Um id novo com regra de jogo** (bloqueia, pegável…) | `sim/tile_ids.hpp` + `sim/src/item_type.cpp` | **Sim** (código C++) |
| **Colocar coisas no mundo** | `assets/maps/*.txt` (à mão ou pelo `game_editor`) | Não |

Regra de ouro: **arte e mapa são dados** (lidos em runtime, sem rebuild);
**tipos de item são código** (o servidor precisa deles compilados, e nunca vê a
arte). Isso é o que mantém o `server-only` sem lib gráfica — ver
[content.md](content.md).

## Onde as coisas moram

```
assets/tilesets/atlas.png   a folha de sprites (uma textura só)
assets/tilesets/atlas.txt   liga cada recorte do PNG a um id/direção
assets/maps/*.txt           mapas autorados
src/sim/include/sim/tile_ids.hpp   os ids (constantes)
src/sim/src/item_type.cpp          build_default_registry(): regras de cada id
tools/gen_placeholder_atlas.py     gera o atlas placeholder (PIL)
tools/gen_dungeon.py               gera o calabouço de exemplo
```

---

## Receita A — um objeto novo (ex.: um barril)

Vamos criar um **barril** (`id 103`) que bloqueia passagem, ponta a ponta. (O
mesmo passo a passo vale para paredes, árvores, baús etc. — muda só as flags.)

### 1. Desenhar o sprite no atlas

O atlas é 256×256. A linha de objetos fica em `y=64` (células de 64×64); os slots
`x=0,64,128` são parede/árvore/caixa, então o **slot 3 em `x=192` está livre**.
Em `tools/gen_placeholder_atlas.py`, desenhe ali (perto do bloco da caixa):

```python
# barril  id 103  -- objeto novo (docs/authoring.md)
bx = 3 * TW  # x = 192, slot livre na linha de blocos
fill_block(px, bx, BLOCK_Y, rgba((150, 110, 60)),
           rgba((96, 68, 40)), rgba((120, 88, 50)), 32)
```

Regere o PNG:

```bash
python3 tools/gen_placeholder_atlas.py assets/tilesets
```

> Trabalhando com arte de verdade em vez do gerador? Basta pintar o recorte no
> `atlas.png` no mesmo lugar; o gerador é só um placeholder.

### 2. Vincular o id ao recorte — `atlas.txt`

Adicione **uma linha** (é isto que responde "qual sprite é o objeto 103?"):

```
object      103     192  64   64  64  -32      -32
#           id      x    y    w   h   origin_x origin_y
```

`origin` é o deslocamento do **vértice de cima do tile** até o canto superior
esquerdo do sprite. Valores canônicos: bloco 64×64 → `-32 -32`; chão 64×32 →
`-32 0`; ator 32×48 → `-16 -32`. Ver [sprites.md](sprites.md).

**Só com os passos 1–2 o cliente já desenha o id 103** — mas a simulação ainda não
conhece esse id. Se você parar aqui, é uma decoração muda.

### 3. Declarar o id — `sim/tile_ids.hpp`

```cpp
constexpr TileId kBarrel = 103;
```

Ids são contrato: uma vez usados num mapa/rede, **não mude o significado** de um id
já existente (recicle números aposentados, não reatribua). Ground fica na faixa
baixa, objetos em 100+, por convenção (ver os ids atuais na tabela mais abaixo).

### 4. Dar regras de jogo — `build_default_registry()`

Em `src/sim/src/item_type.cpp`, registre o tipo com suas flags:

```cpp
registry.add(ItemType{tiles::kBarrel, ItemFlag::BlocksWalk, 0U, 1U});
```

A partir daqui o barril **bloqueia passagem automaticamente**: o gerador de mapa e
o parser derivam `Tile::blocking` da flag `BlocksWalk` — você nunca escreve "isto
bloqueia" à mão. Quer um enfeite que **não** bloqueia (dá pra pisar em cima)? Use
`ItemFlags{}` (nenhuma flag). Combine com `|`:
`ItemFlag::BlocksWalk | ItemFlag::Pickable`.

Recompile (mexeu em C++):

```bash
cmake --build --preset debug -j
```

### 5. Colocar no mundo

Abra o editor — o barril já aparece na paleta (ela é montada dos ids do catálogo
que têm sprite):

```bash
./build/debug/bin/game_editor --map assets/maps/dungeon.txt
```

Escolha o barril (clique na paleta ou `Tab`/`0`–`9`), clique pra pintar, `S` pra
salvar. Ou, à mão, no `.txt`: `legend b 3 103` (chão pedra + barril) e use `b` na
grade. Ver [maps.md](maps.md).

---

## Receita B — um tipo de chão novo (ex.: areia)

Chão é igual, com duas diferenças: a flag `Ground` e o tamanho do sprite (diamante
64×32, não bloco 64×64).

1. **Sprite**: a linha de chão (`y=0`) já está cheia (4 slots). Use uma região
   livre do atlas (ex.: `y=176`) e desenhe um diamante 64×32 lá.
2. **`atlas.txt`**: `ground 5 0 176 64 32 -32 0`.
3. **`tile_ids.hpp`**: `constexpr TileId kSand = 5;`
4. **Registry**: `registry.add(ItemType{tiles::kSand, ItemFlag::Ground, 0U, 1U});`
   (`Ground` = pode ser camada de chão; sem `BlocksWalk` = caminhável. Água é o
   caso raro de chão que bloqueia: `ItemFlag::Ground | ItemFlag::BlocksWalk`.)
5. Pinte com o editor ou use na legenda de um mapa.

---

## Receita C — um mapa novo

Duas formas, e as duas terminam no mesmo `TileMap` (o `sim/` não sabe qual foi a
origem):

**Pelo editor** (recomendado):

```bash
./build/debug/bin/game_editor --map assets/maps/floresta.txt
```

Sem arquivo, ele abre uma tela de pedra 48×32 em branco. Pinte, `S` pra salvar.

**À mão**, um `.txt` no formato (detalhes em [maps.md](maps.md)):

```
size 12 6 1
legend . 1          # grama (id 1)
legend T 1 101      # grama + árvore
legend ~ 4          # água
legend @ 1
spawn @
floor 0
............
..TT....~~..
..@.....~~..
....TT......
............
............
```

Espaço em branco = vazio (buraco não-caminhável). Um caractere fora da legenda é
erro (pega typo). O cliente carrega `maps/dungeon.txt` por padrão no solo; para
ver outro mapa hoje, salve por cima dele (um seletor de mapa é trabalho futuro).

---

## Tabelas de referência

### Ids em uso

| Id | Nome | Tipo |
|---|---|---|
| 1 | grama | chão |
| 2 | terra | chão |
| 3 | pedra | chão |
| 4 | água | chão (bloqueia) |
| 100 | parede | objeto (bloqueia passo e visão) |
| 101 | árvore | objeto (bloqueia passo) |
| 102 | caixa | objeto (bloqueia, pegável) |
| 200 | ator | reservado (não é tile) |

Próximo id livre de objeto: **103**. De chão: **5**.

### Flags de `ItemType` (`sim/item_type.hpp`)

| Flag | Significado |
|---|---|
| `BlocksWalk` | não dá pra pisar (vira o `Tile::blocking`) |
| `BlocksSight` | corta linha de visão (reservado; nada lê ainda) |
| `Ground` | pode ser camada de chão |
| `Pickable` | pode ir pro inventário |
| `Stackable` | pilhas iguais se juntam (ver `max_stack`) |
| `Container` | guarda outros itens |

`ItemType` = `{ id, flags, weight, max_stack }`. Combine flags com `|`.

### Tipos de linha do `atlas.txt`

| Tipo | Campos |
|---|---|
| `ground` | `id x y w h origin_x origin_y` |
| `object` | `id x y w h origin_x origin_y` |
| `actor` | `dir(0-7) x y w h origin_x origin_y` |
| `highlight` | `x y w h origin_x origin_y` (cursor) |
| `solid` | `x y w h` (texel branco pra fundos de UI) |

Origins canônicos: chão `-32 0`, bloco 64×64 `-32 -32`, ator 32×48 `-16 -32`.

## Checklist e erros comuns

- **Mudou C++ (`tile_ids.hpp`/`item_type.cpp`)?** Recompile. Só arte/`atlas.txt`/
  mapa? Não precisa — são lidos em runtime da árvore-fonte.
- **Sprite não aparece?** Confira a linha no `atlas.txt` (id certo, dentro de
  256×256) e que o id está registrado no catálogo. Id sem sprite não entra na
  paleta do editor.
- **O objeto não bloqueia (ou bloqueia sem querer)?** É a flag `BlocksWalk` no
  registry — nunca se escreve blocking no mapa.
- **Compile com `-Werror` antes de dizer que está pronto** (`-DGAME_WERROR=ON`),
  e não coloque I/O nem SDL em `sim/` (o `check-layering.sh` reprova).
- **Não recicle ids** já usados por mapas salvos ou pela rede.
