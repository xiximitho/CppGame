# Pendências — plataforma de conteúdo e persistência

Handoff da sessão que trouxe o SQLite para o projeto. O contexto de fundo está em
[content.md](content.md) (o pipeline, já reescrito para a decisão nova) e
[authoring.md](authoring.md) (o cookbook, já atualizado).

## O que já está pronto

| # | Etapa | Commit |
|---|---|---|
| 1 | Fonte bitmap no atlas + `client::ui` (texto na tela) | `a133675` |
| 2 | `bitstream` para `core/` + codec do blob (`sim/content_blob.hpp`) | `c0392ac` |
| 3 | SQLite pinado por SHA3-256 + wrapper `store::Db` | `0936618` |
| 4 | `content.db`: schema, seed, leitura no servidor e no editor | `788582c` |
| 8 | `tools/bake`: banco → `content.bin` que o cliente lê | `300b5fb` |
| 6 | Modo item do `game_editor` (`F2`) | este commit |

Estado utilizável hoje: `./build/debug/bin/game_editor`, `F2`, `N` cria item,
setas editam, `S` salva no banco **e** regera o `assets/content.bin`. O servidor lê
o `content.db` direto; o cliente lê o blob.

## Pendências, em ordem de dependência

### A. Seletor de sprite no modo item  (a parte que falta do pedido original)

O pedido era "selecionar o sprite do item, e configurações, ataque defesa". As
configurações estão feitas; **escolher o sprite não**. Hoje um item novo nasce sem
sprite e por isso não entra na paleta do mapa nem aparece na tela — as regras
funcionam, a arte não.

O que falta:

- Desenhar o `atlas.png` num painel do modo item, com grade do tamanho certo por
  tipo (chão 64×32, objeto 64×64, ícone de equipamento 16×16).
- Clique numa célula grava a linha correspondente no `assets/tilesets/atlas.txt`
  (`ground|object|item <id> <x> <y> <w> <h> <origin_x> <origin_y>`).
- **Não existe writer de `atlas.txt` ainda.** Só o parser
  (`Tileset::parse_atlas_meta`). Precisa ser edição por linha: achar a linha do id
  e substituir, ou adicionar no fim.
- Origins canônicos estão em `docs/sprites.md`: chão `-32 0`, bloco 64×64
  `-32 -32`, ator 32×48 `-16 -32`.

Cuidado de camada: a linha do `atlas.txt` é **apresentação**, só cliente. Ela não
entra no `content.db` nem no blob — é o que mantém o servidor sem saber de arte.

### B. Hash de conteúdo no Hello + bump de `kProtocolVersion`  (era etapa 5)

Hoje nada impede um cliente e um servidor rodarem catálogos diferentes: o servidor
lê o `.db`, o cliente lê o blob, e se alguém editar itens sem rebakear eles
divergem em silêncio — muda o que bloqueia e quanto ataca.

O truque que resolve, já decidido: o servidor serializa o registry em memória com
`sim::write_content_blob` **só para hashear**, então o hash prova igualdade
semântica mesmo com fontes diferentes.

- `net::HelloMsg` ganha o hash; o servidor rejeita divergência como já rejeita
  versão de protocolo errada.
- `net::kProtocolVersion` está em **5** hoje (`net/protocol.hpp:21`) → vai para 6.
- Não existe função de hash no projeto. Um FNV-1a 64 em `core/` resolve; não precisa
  ser criptográfico, precisa detectar diferença.

### C. Persistência de jogador  (era etapa 9)

O schema **já existe** (`store/src/schema.cpp`, `kPlayerV1`): `accounts`,
`characters`, `character_items`, com `user_version` próprio. Nada escreve nelas
ainda. `store::open_player_db()` também ainda não existe — só
`open_content_db()`; siga o mesmo formato (`Db::open` + `apply_player_migrations`).

Onde plugar, em `src/server/main.cpp`:

- O kit inicial hardcoded está nas linhas ~258–267 do handler de Hello (espada e
  armadura equipadas, arco e escudo na mochila) — isso passa a ser o fallback de
  personagem novo, e um personagem existente carrega posição/equipamento/inventário
  do banco.
- `HelloMsg` tem `protocol` e `name`, **sem senha**. Autenticação de verdade é uma
  decisão à parte; hoje o nome é a identidade.
- Salvar no logout (o `case` de desconexão do peer) e periodicamente.
- `players.db` já está no `.gitignore`.

Detalhe que vai morder: `character_items` tem `CHECK ((slot IS NULL) <> (bag_index
IS NULL))` — exatamente um dos dois é preenchido. Equipado usa `slot`, mochila usa
`bag_index`.

### D. Limpezas conhecidas

- **`sim::build_default_registry()` tem dois papéis hoje**: semente do banco e
  fallback do cliente quando falta `content.bin`. Os 4 testes que a usam
  (`test_world`, `test_combat`, `test_map_io`, `test_item_type`) ainda dependem dela.
  Não é urgente, mas é a última cópia hardcoded do catálogo e vale decidir se
  continua sendo a semente ou se vira um fixture de teste.
- **`assets/content.db` está commitado** (é conteúdo, fonte da verdade). É binário:
  duas pessoas editando itens ao mesmo tempo geram conflito que o git não resolve.
  Se isso incomodar, o caminho é um dump `.sql` diffável ao lado.
- **Editar item pelo teclado não foi verificado end-to-end**: `SDL_VIDEODRIVER=dummy`
  não entrega teclas, então o formulário foi verificado por screenshot e os caminhos
  de banco (`save_item`, `next_free_item_id`, `retire_item`, bake) por teste
  unitário. O laço "digitar no editor → aparecer no jogo" só foi exercitado
  mexendo no banco por fora.
- **`tools/gen_dungeon.py` e o Pillow**: o gerador de atlas precisa de Pillow, que
  os `scripts/setup-*.sh` não instalam. Documentado no `authoring.md`.
- **`docs/roadmap.md` item 3 ("Persistência")** continua descrevendo o estado
  antigo ("Nada é salvo") e deve ser atualizado quando C ficar pronto.

## Invariantes que esta trilha adicionou (não quebre de passagem)

- **O cliente não abre banco.** No Android os assets estão dentro do pacote e não
  são arquivos; SQLite quer caminho real. O `check-layering.sh` reprova
  `<sqlite3.h>` e link de `sqlite3`/`game_store` em `src/client/` e
  `src/platform/`. As duas regras foram testadas disparando de verdade.
- **`server-only` linka SQLite e isso está certo.** O invariante do preset é "sem
  SDL, sem lib gráfica", não "sem dependência".
- **Id de item é contrato.** Nunca reciclado; `retired_item_ids` guarda os
  aposentados, e `next_free_item_id` tira o máximo das duas tabelas.
- **SQLite é pinado por SHA3-256 do zip oficial**, não por SHA de commit — não é
  desenvolvido no GitHub e o amalgamation é gerado. O `verify-deps.sh` re-baixa e
  re-hasheia.
- **Blob recusado não deixa registry pela metade**: o parse monta local e só move
  para o destino depois de validar tudo. Vale para `read_content_blob` e para
  `load_item_types`.
