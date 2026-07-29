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
floor <z>                                # as próximas <h> linhas são a grade
<h linhas de até w caracteres>
```

- `legend`: `ground_id`/`object_id` são `sim::TileId` (os mesmos de
  `tile_ids.hpp`). `object_id` é opcional; ausente = sem objeto.
- `spawn`: opcional. Uma sessão pode preferir esse tile a um walkable aleatório
  (ainda não obrigatório).
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

## O calabouço de exemplo

`assets/maps/dungeon.txt` (56×40) é gerado por `tools/gen_dungeon.py` (salas +
corredores escavados na rocha, um poço d'água, caixas, spawn). Regerar:

```bash
python3 tools/gen_dungeon.py assets/maps/dungeon.txt
```

O cliente carrega `maps/dungeon.txt` por padrão no solo; o servidor tenta
`assets/maps/dungeon.txt` relativo ao diretório de execução.

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
  mas não deixa recolocá-lo; a sessão ainda usa walkable aleatório).
- **Multi-andar**: edita só o andar 0 por enquanto (o formato e o writer já
  suportam vários; falta UI para trocar de andar).
- **Caminho de asset do servidor** robusto (hoje é relativo ao CWD, com fallback).
- Eventualmente, assar o `.txt` para o blob binário do content.md.
