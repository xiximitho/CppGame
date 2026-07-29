# Sprites: como a arte chega na tela e como vincular um sprite a um objeto

Este doc é a parte prática de [content.md](content.md): onde os pixels moram, como
são carregados, e a receita para adicionar um objeto novo com sprite.

## De onde vêm os sprites

Há **duas fontes**, e o cliente escolhe em runtime (`Tileset::load`):

1. **Atlas PNG** em `assets/tilesets/atlas.png` + `assets/tilesets/atlas.txt`.
   Se ambos existem e decodificam, é o que se usa. O PNG é decodificado pelo
   **SDL_image** (backend stb, sem libpng do sistema), convertido para RGBA e
   enviado à GPU como uma textura só.
2. **Procedural** (`Tileset::build_procedural`), desenhado em código. É o
   fallback quando o atlas falta ou falha. Garante que um clone limpo compila e
   roda sem asset nenhum.

Nada no resto do render sabe qual fonte foi usada: os dois preenchem a mesma
tabela de `AtlasEntry`, e o `Renderer2D` só vê uma textura + UVs.

O atlas placeholder atual é gerado por `tools/gen_placeholder_atlas.py` (PIL). É
tooling descartável — a pipeline final assa o atlas a partir do SQLite
(content.md). Para regerar:

```bash
python3 tools/gen_placeholder_atlas.py assets/tilesets
```

## O arquivo de binding: `assets/tilesets/atlas.txt`

Cada linha vincula uma **região do PNG** a um **id** (ou direção de ator). É este
arquivo que responde "qual sprite é o objeto 102?". Formato, separado por espaços,
`#` inicia comentário:

```
# kind      id/dir  x    y    w   h   origin_x origin_y
ground      1       0    0    64  32  -32      0
object      100     0    64   64  64  -32      -32
actor       0       0    128  32  48  -16      -32
highlight           0    32   64  32  -32      0
```

- `kind`: `ground`, `object`, `actor` ou `highlight`.
- `id/dir`: para `ground`/`object` é o `sim::TileId`; para `actor` é a direção
  0..7 (`sim::Direction`); `highlight` não tem.
- `x y w h`: retângulo em **pixels** dentro do PNG.
- `origin_x origin_y`: deslocamento do **vértice de cima do tile** até o canto
  superior-esquerdo do sprite. Isto — não uma posição — é o que faz um bloco 64×64
  e um ator 32×48 caírem certos no mesmo tile 64×32. Ver `client/iso.hpp`. Valores
  canônicos: chão `-32 0`, bloco 64×64 `-32 -32`, ator 32×48 `-16 -32`.

Trocar só a arte (redesenhar o PNG) não exige recompilar nada. Mudar o layout
(mover regiões) é editar o `atlas.txt`. Só quando o *comportamento de jogo* muda é
que se toca em C++.

## Receita: adicionar um objeto novo com sprite

Exemplo real já no código: a **caixa** (`kCrate`, id 102). Os passos, e o que
cada camada precisa saber (ver a separação em content.md):

1. **Desenhar o sprite no atlas.** Em `tools/gen_placeholder_atlas.py`, uma região
   livre do PNG (aqui o slot 2 da linha de blocos, `x=128,y=64`). Regerar o PNG.

2. **Vincular no `atlas.txt`** — a linha que amarra o id à região:
   ```
   object      102     128  64   64  64  -32      -32
   ```
   Só isto já basta para o **cliente** desenhar o id 102. Mas o id ainda não existe
   para a simulação.

3. **Declarar o id** em `sim/tile_ids.hpp`:
   ```cpp
   constexpr TileId kCrate = 102;
   ```

4. **Registrar o tipo** em `build_default_registry()` (`sim/src/item_type.cpp`) com
   as flags de jogo — é isto que o **servidor** entende:
   ```cpp
   registry.add(ItemType{tiles::kCrate,
                         ItemFlag::BlocksWalk | ItemFlag::Pickable, 40U, 1U});
   ```
   A partir daqui a caixa **bloqueia passagem** automaticamente: o gerador de mapa
   deriva `Tile::blocking` da flag `BlocksWalk` (não há mais bool na mão).

5. **Colocar no mundo.** Onde algo é posto no mapa, use `place_object(...)`, que
   deriva o blocking do registry. Aqui, `scatter_crates` em `map_gen.cpp`.

Passos 1–2 são **apresentação** (cliente). 3–5 são **regra/dado** (servidor + sim).
Um objeto que o servidor precisa validar (bloqueia, é pegável) exige 3–4; um enfeite
puramente visual poderia viver só em 1–2 com um id que o sim trata como inócuo.

## Escala de arte (Grimhold)

O mockup em `claude_design/Grimhold_*` mira tile 64px, projeção 2:1, paredes ~130px
e atores/criaturas de várias alturas de tile (até ~2,3). Isso é só apresentação:
`origin_y` cuida de sprites altos caírem no tile certo, e nada disso muda `sim/`.
O placeholder atual usa tile 64×32 e blocos 64×64.
