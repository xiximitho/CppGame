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
| **Um id novo com regra de jogo** (bloqueia, pegável, ataque, defesa, alcance…) | `assets/content.db` (pelo `game_editor`, tecla `F2`) | Não |
| **Colocar coisas no mundo** | `assets/maps/*.txt` (à mão ou pelo `game_editor`) | Não |
| **Balancear um mob** (velocidade, hp, dano, aggro…) | `assets/monsters.txt` | Não |
| **Animar um mob** (frames de caminhada, direções, tamanho de célula) | `assets/tilesets/atlas.txt` (pelo `game_editor`, tecla `F4`) | Não |

Regra de ouro: **tudo é dado**. Arte e mapa são lidos em runtime da árvore-fonte;
tipos de item vivem no `assets/content.db` e são editados pelo `game_editor`
(`F2` abre o modo item). O que continua sendo verdade é a separação: o servidor
nunca vê a arte, e é isso que mantém o `server-only` sem lib gráfica — ver
[content.md](content.md).

> Item novo agora é assim: abra o editor, `F2`, `N` para criar, edite os campos,
> `S` para salvar. O `S` grava no banco **e** regera o `assets/content.bin` que o
> cliente lê, então não sobra passo de terminal.
>
> **Vincular o sprite também é pelo editor**: no campo `sprite`, `enter` abre um
> seletor que desenha o `atlas.png` com uma grade do tamanho certo; clique numa
> célula e a linha do `atlas.txt` é escrita (com os origins canônicos) e o tileset
> recarrega na hora. O campo `sprite kind` escolhe se está vinculando chão, objeto
> de mapa ou ícone de inventário — um item pode precisar de mais de um.
>
> O que ainda é trabalho de arte: **desenhar** o recorte no `atlas.png`. O seletor
> escolhe entre células existentes; ele não pinta.

> **Animação de mob é o modo `F4`** (`docs/animation.md`). Uma classe de mob tem duas
> metades: os números em `assets/monsters.txt` (simulação, o servidor lê) e os sprites
> no `atlas.txt`, ligados pelo `appearance`. O `F4` edita a segunda — célula inicial,
> tamanho de célula, direções (4 ou 8), frames, origin — e mostra todos os frames de
> todas as direções, com uma célula animada pela mesma função que o jogo usa. Trazer
> pixels novos de uma folha de arte é do `tools/import_otsp.py`; o `F4` vincula o que
> já está no atlas.

## Onde as coisas moram

```
assets/tilesets/atlas.png   a folha de sprites (uma textura só)
assets/tilesets/atlas.txt   liga cada recorte do PNG a um id/direção
assets/maps/*.txt           mapas autorados (tiles, spawn, mobs e ninhos)
assets/monsters.txt         as classes de mob (velocidade, hp, dano, aggro, loot)
assets/content.db           o catálogo de itens (SQLite; editado pelo game_editor)
assets/content.bin          blob derivado do banco, que o CLIENTE lê (game_bake)
src/sim/include/sim/tile_ids.hpp   ids que o próprio engine referencia por nome
src/sim/src/item_type.cpp          build_default_registry(): a SEMENTE do banco
tools/gen_placeholder_atlas.py     gera o atlas placeholder (PIL)
tools/import_otsp.py               traz uma criatura de uma folha estilo Tibia
assets/tibia_like/                 as folhas de origem (CC BY 4.0, ver CREDITS.md)
tools/gen_dungeon.py               gera o calabouço de exemplo
tools/gen_maps.py                  gera os outros mapas (floresta, vila, ...)
```

---

## Receita A — um objeto novo (ex.: um barril)

Vamos criar um **barril** (`id 105`) que bloqueia passagem, ponta a ponta. (O
mesmo passo a passo vale para paredes, árvores, baús etc. — muda só as flags.)
103 e 104 já são as escadas, e id não se recicla: o editor escolhe o próximo livre
com `N`, e é isso que você deve usar em vez de digitar um número.

### 1. Desenhar o sprite no atlas

O atlas é 256×440 (cresceu para caber escadas e mobs). A linha de objetos fica em
`y=64` (células de 64×64); os slots `x=0,64,128` são parede/árvore/caixa, então o
**slot 3 em `x=192` está livre** — as escadas foram para a faixa nova em `y=256`
justamente porque precisavam de duas vagas.
Em `tools/gen_placeholder_atlas.py`, desenhe ali (perto do bloco da caixa):

```python
# barril  id 105  -- objeto novo (docs/authoring.md)
bx = 3 * TW  # x = 192, slot livre na linha de blocos
fill_block(px, bx, BLOCK_Y, rgba((150, 110, 60)),
           rgba((96, 68, 40)), rgba((120, 88, 50)), 32)
```

Regere o PNG:

```bash
python3 tools/gen_placeholder_atlas.py assets/tilesets
```

O gerador precisa do Pillow, que os `scripts/setup-*.sh` não instalam (o atlas
vai commitado, então quem não mexe em arte nunca precisa dele). Se faltar:
`python3 -m venv .venv && .venv/bin/pip install Pillow`, e rode pelo
`.venv/bin/python`.

> Trabalhando com arte de verdade em vez do gerador? Basta pintar o recorte no
> `atlas.png` no mesmo lugar; o gerador é só um placeholder.

### 2. Vincular o id ao recorte

**Pelo editor** (recomendado): no modo item, campo `sprite`, `enter`. O seletor
desenha o atlas com grade de 64×64 para objeto (64×32 para chão, 16×16 para ícone),
a célula já vinculada aparece em verde, e o clique escreve a linha. Os origins são
calculados, não digitados.

**À mão**, é uma linha (é isto que responde "qual sprite é o objeto 103?"):

```
object      105     192  64   64  64  -32      -32
#           id      x    y    w   h   origin_x origin_y
```

`origin` é o deslocamento do **vértice de cima do tile** até o canto superior
esquerdo do sprite. Valores canônicos: bloco 64×64 → `-32 -32`; chão 64×32 →
`-32 0`; ator 32×48 → `-16 -32`. Ver [sprites.md](sprites.md).

Para vincular em lote (uma folha de arte nova, dezenas de ids), existe também
`game_editor --bind-sprite object:105:3:1` — `kind:id:coluna:linha` em células,
sem abrir janela.

**Só com os passos 1–2 o cliente já desenha o id 105** — mas a simulação ainda não
conhece esse id. Se você parar aqui, é uma decoração muda.

### 3 e 4. Criar o id e dar regras — no editor, sem recompilar

```bash
./build/debug/bin/game_editor      # F2 abre o modo item
```

`N` cria um item novo na faixa de id do item selecionado (selecione uma pedra e
ganha id de chão; selecione uma espada e ganha id de equipamento). Setas movem
entre campos e mudam valores (`shift` = passo de 10), dígitos digitam números,
`enter` no campo `name` edita o nome, `S` salva.

O id é escolhido pelo editor e **nunca é reciclado**: `shift+del` aposenta um
item, e o número dele fica reservado para sempre, porque mapa salvo e cliente
antigo referenciam item por número.

Marcando `blocks walk`, o barril **bloqueia passagem automaticamente**: o gerador
de mapa e o parser derivam `Tile::blocking` dessa flag — você nunca escreve "isto
bloqueia" à mão. Quer um enfeite que não bloqueia? Deixe a flag em `no`.

`range (tiles)` só fica editável quando `attack kind` é `ranged`; em melee o
alcance é sempre 1 e o campo aparece esmaecido em vez de aceitar um valor que
nada leria.

### 5. Colocar no mundo

Abra o editor — o barril já aparece na paleta (ela é montada dos ids do catálogo
que têm sprite):

```bash
./build/debug/bin/game_editor --map assets/maps/dungeon.txt
```

Escolha o barril (clique na paleta ou `Tab`/`0`–`9`), clique pra pintar, `S` pra
salvar. Ou, à mão, no `.txt`: `legend b 3 105` (chão pedra + barril) e use `b` na
grade. Ver [maps.md](maps.md).

---

## Receita B — um tipo de chão novo (ex.: areia)

Chão é igual, com duas diferenças: a flag `Ground` e o tamanho do sprite (diamante
64×32, não bloco 64×64).

1. **Sprite**: a linha de chão (`y=0`) já está cheia (4 slots). Use uma região
   livre do atlas (ex.: `y=176`) e desenhe um diamante 64×32 lá.
2. **`atlas.txt`**: `ground 5 0 176 64 32 -32 0`.
3. **Id e regras**: no modo item do editor, selecione um chão existente e `N`
   (para o id cair na faixa de chão), marque `is ground` = `yes` e deixe
   `blocks walk` = `no`. Água é o caso raro de chão que bloqueia: as duas em `yes`.
5. Pinte com o editor ou use na legenda de um mapa.

---

## Receita B2 — uma escada

Escada é objeto com flag, não código novo. No modo item do editor (`F2`), num id da
faixa de objeto: `stairs up` = yes (ou `stairs down`), `blocks walk` = **no** (o
ponto é pisar nela), sprite escolhido no seletor. Já existem as duas: ids **103** e
**104**.

No mapa, o par tem que ser simétrico: `<` em `(x,y,z)` e `>` no **mesmo x,y** em
`z+1`, senão só dá para subir. O `tools/gen_maps.py` valida isso (`stair ... leads
nowhere`) e a `torre.txt` é o exemplo pronto.

No editor, do começo ao fim:

1. `F3`, escolha o mapa (ou `--map <nome>`).
2. O mapa tem um andar só? `Ctrl+PgUp` adiciona o de cima. Com a escada no pincel, a
   barra de baixo avisa em laranja quando o andar de destino não existe — é
   exatamente o caso que `apply_stairs` recusa em silêncio, e a fonte da impressão
   de que "escada não funciona".
3. `PgDn` até o andar de baixo, pinte `stairs up` (103) num tile caminhável.
4. `PgUp`, e **no mesmo x,y** pinte `stairs down` (104) — e chão em volta, senão
   você sobe para dentro de uma sala de um tile.
5. `S`. Conferir andando: `game_client --solo --map <nome>`.

O passo 4 é o que se esquece. Subir funciona, descer não, e o mapa parece meio
quebrado; a checagem em `tests/test_shipped_maps.cpp` pega o caso pior (escada que dá
em rocha) nos mapas commitados, mas não olha o seu arquivo antes de você salvar.

---

## Receita B3 — balancear um mob (sem recompilar)

Edite `assets/monsters.txt` e rode. Nada de build, nada de rebake. `step_ticks` é a
velocidade (jogador = 9; maior é mais lento), e o resto está em
[monsters.md](monsters.md).

```
class 3 ogro
  hp           90
  attack       18
  step_ticks   30      # 1 segundo por tile
  aggro        7
```

Chave desconhecida é erro e o arquivo inteiro é recusado, com o motivo no log — aí
vale o catálogo embutido. Se o mob não mudou, a primeira linha do log diz por quê.

## Receita B4 — uma classe de mob nova

1. **Id** em `sim/monster_type.hpp` (`namespace monsters`) e uma `appearance` nova.
   Id de classe é contrato: nunca recicle.
2. **Números**: um bloco `class` no `assets/monsters.txt`, **e** a mesma entrada em
   `default_monsters()` (`src/sim/src/monster_type.cpp`), que é o fallback quando o
   arquivo falta. Os dois têm que concordar. Faça a classe diferir em algo que se
   **sente** — alcance, velocidade, quando ela te nota — e não só em hp.
3. **Arte**: um `draw_*` em `tools/gen_placeholder_atlas.py` e uma faixa em
   `MOB_BANDS` (8 direções; a célula pode ter o tamanho que a criatura pede), depois
   `python3 tools/gen_placeholder_atlas.py --patch assets/tilesets`.

## Receita B5 — colocar mob no mundo

Duas linhas diferentes no `.txt` do mapa:

```
monster 22 11 0 2                 # UM esqueleto, morreu e acabou
spawner 31 13 0 1 3 2 20          # 3 ratos vivos, raio 2, repõe 20s após cada morte
```

Ou um plano em `monster_plan()` / `spawner_plan()` do `gen_maps.py`, que valida tile
caminhável, alcançável e população que caiba no raio. Detalhes em
[monsters.md](monsters.md).

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
erro (pega typo).

Para **ver** o mapa novo, aponte a flag `--map` (não precisa mais salvar por cima
do calabouço):

```bash
./build/debug/bin/game_client --solo --map maps/floresta.txt   # relativo aos assets
./build/debug/bin/game_server --map assets/maps/vila.txt       # relativo ao CWD
```

O padrão continua sendo `maps/dungeon.txt`, e um arquivo ilegível cai no mapa
procedural com `LOG_WARN`. Em rede o mapa é o do servidor; `--map` no cliente vale
para o solo. O cliente também aceita `map=maps/floresta.txt` no `client.cfg`.

Os mapas de exemplo (`floresta`, `vila`, `caverna`, `ilha`, `torre`) saem de
`tools/gen_maps.py`, que valida conectividade antes de gravar — ver
[maps.md](maps.md).

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

Equipamento ocupa 300–308 (espada, arco, escudo, elmo, armadura, pernas, botas,
anel, amuleto). O editor escolhe o próximo id livre por faixa — não escolha à mão,
e não recicle: `retired_item_ids` no banco guarda os aposentados, e o id 200 do
ator já está lá reservado.

### Campos do item (modo item do editor)

| Campo | O que é |
|---|---|
| `name` | só para humanos e ferramentas; **não** vai no blob nem o servidor vê |
| `weight` | em centi-oz |
| `max stack` | só importa com `stackable`; mínimo 1 |
| `equippable` / `slot` | se dá para vestir, e onde |
| `attack` / `defense` | somados aos do portador |
| `attack kind` | `melee` ou `ranged` (só arma) |
| `range (tiles)` | alcance; editável só quando `ranged` |
| `effect id` | efeito visual do ataque (1 = brilho melee, 2 = tiro) |

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
| `font` | `first_ascii count x y cell_w cell_h per_row` (fonte bitmap) |

A linha `font` descreve a **grade inteira** de glifos numa linha só: as células
saem de `first_ascii` em ordem ASCII, `per_row` por faixa, quebrando para a
faixa seguinte. Hoje é `font 32 95 0 224 6 8 42` — os 95 ASCII imprimíveis em
células 6×8 (glifo 5×7 + 1px de espaçamento), ocupando a última faixa livre do
atlas (`y=224..248`). Para corrigir ou adicionar um glifo, edite as 7 linhas
dele na tabela `FONT` do `tools/gen_placeholder_atlas.py` (`#` é pixel aceso) e
regere o PNG — não precisa recompilar. **Crescer o conjunto de glifos exige
crescer o atlas**, porque não sobra mais faixa livre.

Quem desenha texto é `client::ui::text()` (`src/client/include/client/ui.hpp`),
junto de `ui::fill()` e `ui::sprite()`. Os glifos saem da mesma textura de todo
o resto, então texto **não custa draw call nenhum** — a cena inteira continua em
1 (o `game_editor` loga o número junto do `--screenshot`).

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
