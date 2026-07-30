# Conteúdo: itens, tipos e o pipeline de dados

> Status: **implementado**, com uma revisão de decisão no meio do caminho (ver
> "O pipeline" abaixo). Existe hoje: `content.db` em SQLite como fonte da verdade,
> lido direto pelo servidor; `tools/bake` gerando o `content.bin` que o cliente lê;
> o modo item do `game_editor` (`F2`) editando regras e sprite; e o hash de conteúdo
> no Hello impedindo cliente e servidor de divergirem.
>
> Falta: persistência de jogador (o schema existe, nada escreve nele) — ver
> [pendencias.md](pendencias.md).
>
> As escolhas aqui tocam invariantes que o `CLAUDE.md` protege de propósito (pureza
> do `sim/`, `server-only` sem lib gráfica, determinismo por seed, versão de
> protocolo).

## O problema

Hoje um tile carrega dois números (`ground`, `object`) e um `bool blocking`, e é
só. Os ids vivem como constantes em `sim/tile_ids.hpp` (chão 1-4, objeto 100-101,
ator 200), e o significado visual está espalhado em `tileset.cpp`. Isso serve ao
protótipo, mas não serve para **montar mapas com itens**: não há nome, não há
propriedade de jogo além de "bloqueia ou não", não há empilhável/pegável/container,
e não há ferramenta de autoria.

Queremos uma plataforma de itens/tipos no estilo Tibia — um catálogo de tipos com
propriedades — e um jeito fácil de montar mapas colocando esses itens.

## O que o código já decidiu (e não vamos quebrar)

Duas frases do repositório mandam no design inteiro:

- `sim/tile_map.hpp`: *"The simulation never interprets them beyond `blocking`;
  art is purely a client concern."*
- `sim/tile_ids.hpp`: *"The server keeps needing nothing but the numbers."*

Isto é o que mantém três coisas vivas: o `sim/` não abre arquivo nem lê relógio
(regra de camadas), o preset `server-only` compila sem SDL nem lib gráfica, e o
mapa por seed bate byte-a-byte entre Linux e Windows. Toda a proposta abaixo é
uma forma de crescer o conteúdo **sem** tocar nessas três.

## Três conhecimentos sobre o mesmo id

O erro fácil é tratar "item" como uma coisa só. São três, e cada uma vive numa
camada diferente:

| Conhecimento | Exemplo | Quem precisa | Onde vive |
|---|---|---|---|
| **Número** | `1723` | todos, inclusive a rede | `TileId` / `ItemTypeId` (uint16) |
| **Regra de jogo** | bloqueia passo, bloqueia visão, pegável, empilhável, peso | servidor **e** `sim/` | `sim::ItemType` (dado puro) |
| **Apresentação** | sprite no atlas, frames de animação, nome de UI | só o cliente | `client::ItemArt` |

O `sim/` e o servidor **nunca** veem a coluna de apresentação. O cliente vê as
três. A rede carrega só a primeira. Essa separação não é burocracia — é o que
deixa o `server-only` existir.

## Espaço de ids: plano, com flags — não por faixa

As faixas atuais (chão 1-4, objeto 100-101) codificam categoria no intervalo do
número. Isso não escala e é frágil. O modelo Tibia — que é o que queremos — usa um
**espaço único de item-id, e as propriedades dizem o comportamento**:

```cpp
// sim/include/sim/item_type.hpp  — PURO. Sem I/O, sem SDL, sem <random>.
namespace sim {

using ItemTypeId = std::uint16_t;
constexpr ItemTypeId kItemNone = 0;

enum class ItemFlag : std::uint32_t {
    BlocksWalk   = 1u << 0,   // substitui o antigo Tile::blocking
    BlocksSight  = 1u << 1,   // para line-of-sight (ainda não existe)
    Pickable     = 1u << 2,
    Stackable    = 1u << 3,
    Container    = 1u << 4,
    Ground       = 1u << 5,   // pode ser camada de chão
    // ...
};

struct ItemType {
    ItemTypeId       id     = kItemNone;
    ItemFlags        flags;          // bitset das ItemFlag
    std::uint16_t    weight = 0;
    std::uint8_t     max_stack = 1;
    // NADA de sprite aqui.
};

}  // namespace sim
```

`Tile::blocking` deixa de ser um `bool` avulso e passa a ser derivado de
`registry.get(object_id).has(ItemFlag::BlocksWalk)` — **mas cacheado no tile**,
não consultado no hot loop. O comentário atual em `tile_map.hpp` já explica por que
o flag mora no tile e não é derivado toda hora: o servidor não pode precisar de um
arquivo de definição para responder "posso andar aqui?" a cada tick. Isso continua
verdade: o registry é carregado uma vez no boot e o `blocking` é gravado no tile no
momento em que o item é colocado.

### O registry é uma tabela const em memória

```cpp
// sim/ — o World recebe uma referência const disto. É dado puro, carregado uma
// vez antes de o primeiro tick rodar. Determinístico porque nunca muda em runtime.
class ItemTypeRegistry {
public:
    const ItemType& get(ItemTypeId id) const;   // id desconhecido -> kItemNone
    std::size_t size() const;
private:
    std::vector<ItemType> by_id_;                // denso, indexado por id
};
```

Quem **preenche** esse registry não é o `sim/` — é o `server`/`platform` no boot,
lendo o blob de conteúdo (ver pipeline abaixo). O `sim/` só consome.

## O pipeline: SQLite é a fonte da verdade; o blob é o caminho do cliente

> **Revisado.** A versão anterior deste doc dizia que o SQLite era só ferramenta de
> autoria e "nunca linkado no `game_client` nem no `game_server`". A decisão mudou
> para o servidor: ele **lê o `content.db` direto**. O cliente continua lendo o
> blob, e isso não é simetria perdida por descuido — é a única forma de o cliente
> funcionar no Android.

```
   AUTORIA + SERVIDOR                        CLIENTE
   ┌───────────────────────┐   tools/bake   ┌────────────────────────────┐
   │  content.db (SQLite)  │───────────────▶│  content.bin (blob)         │
   │   items               │                │  lido por platform::vfs     │
   │   accounts/characters │                │  ↓ read_content_blob        │
   └───────────────────────┘                │  ItemTypeRegistry (const)   │
      ▲            │                        └────────────────────────────┘
      │            └─ servidor abre no boot            ▲
      └─ editor de item escreve aqui       sim/ vê só isto: dado puro
```

**Quem linka SQLite:** o servidor e as ferramentas (`GAME_BUILD_TOOLS`). **Nunca o
cliente** — e o `check-layering.sh` reprova tanto o `#include <sqlite3.h>` quanto o
link de `sqlite3`/`game_store` em `src/client/` e `src/platform/`.

Por que o cliente não pode abrir o banco, apesar de o servidor poder:

- **Android e iOS.** Os assets do cliente ficam **dentro do pacote e não são
  arquivos**; `platform::vfs` existe exatamente por isso. SQLite quer um caminho
  real para abrir e fazer seek. Um cliente que abre banco funciona no desktop e
  falha no device — meses depois. É o mesmo invariante que o `CLAUDE.md` protege
  para `<fstream>`.
- **Conteúdo é minúsculo e só-leitura.** O registry já é um `vector<ItemType>`
  indexado por id: um bounds check e um índice. Um motor de query não compra nada.

E por que o servidor **pode**, apesar do `server-only`:

- O invariante do preset é "sem SDL, sem lib gráfica" — não "sem dependência".
  SQLite é um `.c` amalgamado, sem dependência de sistema. O preset compila e linka
  hoje, com `GAME_BUILD_TOOLS=OFF`.
- Ler o `.db` direto elimina o passo de bake do loop de dev do servidor.
- O risco disso seria cliente e servidor divergirem de conteúdo. Não divergem, e
  isso está **implementado**: `sim::content_hash()` serializa o registry em memória
  com `write_content_blob` só para hashear (FNV-1a 64, `core/hash.hpp`), e o
  `net::HelloMsg` carrega esse digest. O servidor rejeita divergência como rejeita
  versão de protocolo errada, com a mensagem `content mismatch: re-run game_bake`.
  Hashear a forma **serializada** e não os structs é o que torna os dois lados
  comparáveis: bytes de padding no `ItemType` não são especificados.

### Como o SQLite é pinado

Ele é a **exceção** à regra "pin por SHA de commit": não é desenvolvido no GitHub e
o amalgamation é gerado, não versionado. O zip oficial é pinado pelo **SHA3-256**
publicado em `sqlite.org/download.html` — imutável do mesmo jeito que um commit, e
diferente de uma tag. O `verify-deps.sh` baixa e re-hasheia para conferir.

O wrapper é `src/store/` (`store::Db`, `store::Stmt`): RAII, sem ORM, sem query
builder. Nenhum tipo `sqlite3` aparece em `store/db.hpp`, então o `sqlite3.h` fica
contido nesse diretório. Toda conexão nasce com WAL, `synchronous = NORMAL`,
`foreign_keys = ON` (o SQLite deixa OFF por padrão, e o schema depende deles) e
busy timeout — num lugar só, para nenhum chamador esquecer.

### Formato do blob

Implementado em `sim/content_blob.hpp` (`write_content_blob` /
`read_content_blob`), serializado com o `BitWriter`/`BitReader` (mesma disciplina
de overflow grudento: confere `overflowed()` **uma vez no fim**).

> **Onde o `bitstream` mora.** Ele saiu de `net/` para `core/`. O registry é dado
> de `sim/`, e `sim/` **não pode** incluir `net/` — o `check-layering.sh` reprova,
> porque `net/` depende de `sim/` e não o contrário. Como `BitWriter` não sabe
> nada de socket nem de pacote (é bytes entra, bytes sai), `core/` é o lugar certo
> e a rede continua sendo só o seu maior usuário. É `core::BitWriter` hoje.

Escrita e leitura vivem as duas em `sim/`, de propósito: a escrita é o que o
`tools/bake` chama e a leitura é o que o jogo chama, então um campo nunca é
escrito num layout e lido em outro. O parse é puro — bytes entram, registry sai,
sem I/O — igual ao `sim/map_io.hpp`; quem lê arquivo é o chamador.

```
[magic "GCNT"][u16 content_version][u16 item_count]
  repeat: [u16 id][u32 flags][u16 weight][u8 max_stack]
          [bool equippable][u3 slot][i16 attack][i16 defense]
          [bool ranged][u8 attack_range][u8 effect]
```

O esboço original desta seção parou em `max_stack` porque era anterior ao
combate. A segunda linha e a terceira são os campos de equipamento que o
`ItemType` ganhou depois (`docs/combat.md`), incluindo o `attack_range` em tiles
que só faz sentido quando `ranged` — 125 bits por registro no total.

Mapas **não** estão no blob. Continuam no formato de texto do `sim/map_io.hpp`,
que já existe, é diffável e tem editor. Se algum dia virarem binário, entram como
uma seção nova aqui com bump de `content_version`.

`content_version` (hoje **1**) é irmão do `net::kProtocolVersion`: o runtime
rejeita blob de versão que não entende, em vez de misparsear em silêncio. Um blob
recusado **não** deixa o registry pela metade — o parse monta um registry local e
só o move para o destino depois de validar tudo.

## IDs são contrato, como a versão de protocolo

Uma vez que um item-id foi shippado, **o significado dele não muda**. Um mapa
salvo e um cliente antigo referenciam ids por número; reaproveitar o número `1723`
para outra coisa faz o mapa antigo virar outra coisa sem ninguém notar. Regra:

- ids só crescem; ids aposentados ficam reservados, não são reciclados.
- mudar o *significado de jogo* de um id (ex.: um chão virar bloqueante) é uma
  mudança de conteúdo que exige bump de `content_version` se quebrar mapas antigos.
- a coluna de apresentação (sprite) pode mudar à vontade — é só cliente.

## Como "montar mapas com itens" encaixa

Um mapa autorado é conteúdo como qualquer outro: linhas de
`(x, y, z, ground_id, object_id, ...)` numa tabela `map_tiles` do SQLite, escritas
por um editor. O bake serializa para o blob; o runtime desserializa para um
`TileMap` — a mesma estrutura densa que o gerador por seed já produz. Os dois
caminhos coexistem:

- **mapa por seed** (`sim::Rng`) — continua existindo para mundos procedurais.
- **mapa autorado** (blob) — para conteúdo feito à mão.

Ambos terminam num `TileMap` idêntico; o `sim/` não sabe qual foi a origem. O
editor de mapa em si é um alvo de `tools/` (provavelmente reaproveitando o
`Renderer2D` do cliente) e é assunto de um doc próprio quando chegarmos lá.

## Migração a partir de hoje

1. `sim::ItemType` + `ItemTypeRegistry` (dado puro em `sim/`). Sem I/O.
2. `World` passa a receber `const ItemTypeRegistry&`; `Tile::blocking` passa a ser
   preenchido a partir do flag `BlocksWalk` no momento da colocação.
3. Uma tabela de conteúdo *hardcoded* recria os 6 tipos de hoje (grama, terra,
   pedra, água, parede, árvore) — sem blob ainda, para provar que o registry não
   mudou comportamento nenhum. Testes de movimento/pathfinding têm que passar
   idênticos.
4. `client::ItemArt` (a parte de apresentação) substitui os `unordered_map` do
   `Tileset`, indexado pelo mesmo id.
5. Só então: `tools/bake`, o schema SQLite, e o loader de blob em `platform`.
6. Por último, o editor de mapa.

Cada passo é verificável isolado; nenhum deles precisa do SQLite para os quatro
primeiros existirem.

## Não-objetivos (por ora)

- SQLite em runtime — decidido contra, ver acima.
- Definições de item editáveis pelo jogador em runtime — é servidor autoritativo;
  conteúdo é estático por build.
- Scripting de comportamento de item (Lua etc.) — outra discussão, muito depois.

## Referências no código

- `sim/include/sim/tile_map.hpp` — `Tile`, `TileMap`, o `blocking` que vira flag.
- `sim/include/sim/tile_ids.hpp` — as constantes que este design substitui.
- `core/bitstream.hpp` `BitWriter`/`BitReader` — serialização do blob.
- `sim/content_blob.hpp` — o formato do blob, escrita e leitura.
- `platform/vfs` — o único caminho de leitura de asset.
- `docs/roadmap.md` item 6 — arte de verdade (atlas), que anda junto com isto.
