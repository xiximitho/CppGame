# Pendências — plataforma de conteúdo e persistência

Handoff da sessão que trouxe o SQLite e o editor de itens. Contexto de fundo em
[content.md](content.md) (o pipeline) e [authoring.md](authoring.md) (o cookbook).
Os dois já estão atualizados; não confie no `roadmap.md`, que ficou para trás.

## Pronto e utilizável

| # | Etapa | Commit |
|---|---|---|
| 1 | Fonte bitmap no atlas + `client::ui` (texto na tela) | `a133675` |
| 2 | `bitstream` para `core/` + codec do blob (`sim/content_blob.hpp`) | `c0392ac` |
| 3 | SQLite pinado por SHA3-256 + wrapper `store::Db` | `0936618` |
| 4 | `content.db`: schema, seed, leitura no servidor e no editor | `788582c` |
| 8 | `tools/bake`: banco → `content.bin` que o cliente lê | `300b5fb` |
| 6 | Modo item do `game_editor` (`F2`) | `f3acd1c` |
| 6b | Seletor de sprite + writer de `atlas.txt` | `4554d09` |
| A | Hash de conteúdo no Hello + `kProtocolVersion` 6 | este commit |

**O laço completo funciona.** `./build/debug/bin/game_editor` → `F2` → `N` cria
item → setas editam campos → campo `sprite` + `enter` escolhe o recorte no atlas →
`S` salva. O `S` grava no banco **e** regera o `assets/content.bin`; o binding
reescreve o `atlas.txt` e recarrega o tileset na hora. Nenhum passo de terminal,
nenhum recompilar.

Servidor lê `content.db` direto; cliente lê `content.bin`.

## Pendências, em ordem de dependência

### A. Persistência de jogador

O schema **já existe** (`store/src/schema.cpp`, `kPlayerV1`): `accounts`,
`characters`, `character_items`, com `user_version` próprio e
`apply_player_migrations()` pronta. Nada escreve nelas ainda, e
`store::open_player_db()` ainda não existe — só `open_content_db()`; siga o mesmo
formato (`Db::open` + `apply_player_migrations`).

Onde plugar, em `src/server/main.cpp`:

- O kit inicial hardcoded está no handler de Hello (espada e armadura equipadas,
  arco e escudo na mochila, por volta da linha 270 depois das mudanças desta
  sessão). Isso passa a ser o fallback de personagem novo; personagem existente
  carrega posição, equipamento, inventário e hp do banco.
- `HelloMsg` tem `protocol` e `name`, **sem senha**. Autenticação de verdade é
  decisão à parte; hoje o nome é a identidade.
- Salvar no logout (o `case` de desconexão do peer) e periodicamente.
- `players.db` já está no `.gitignore`.

Detalhe que vai morder: `character_items` tem
`CHECK ((slot IS NULL) <> (bag_index IS NULL))` — exatamente um dos dois é
preenchido. Equipado usa `slot`, mochila usa `bag_index`.

### B. Limpezas conhecidas

- **`Renderer2D` não tem `destroy_texture`.** Rebindar um sprite recarrega o
  tileset, e cada recarga vaza uma textura de 256×256 (256 KB). Aceitável numa
  ferramenta, mas a interface tem só ~10 funções de propósito e essa é a que falta.
  Está comentado em `src/editor/main.cpp` onde o vazamento acontece.
- **O seletor escolhe entre células existentes; não desenha.** Arte nova ainda é
  pintar no `atlas.png` (ou no `tools/gen_placeholder_atlas.py`). Também: o atlas é
  256×256 e a faixa de fonte ocupa `y=224..248` — crescer o conjunto de sprites
  exige crescer o atlas.
- **`sim::build_default_registry()` tem dois papéis**: semente do banco e fallback
  do cliente quando falta `content.bin`. Os 4 testes que a usam (`test_world`,
  `test_combat`, `test_map_io`, `test_item_type`) ainda dependem dela. É a última
  cópia hardcoded do catálogo; vale decidir se continua semente ou vira fixture.
- **`assets/content.db` está commitado** (é conteúdo, fonte da verdade). É binário:
  duas pessoas editando itens ao mesmo tempo geram conflito que o git não resolve.
  Se incomodar, o caminho é um dump `.sql` diffável ao lado.
- **`docs/roadmap.md` item 3 ("Persistência")** ainda diz "Nada é salvo" e a tabela
  de "coisas erradas de propósito" não menciona nada disto. Atualizar quando A
  ficar pronto.
- **`docs/authoring.md`** tem uma tabela de "ids em uso" que agora é redundante com
  o banco — o editor mostra a lista real. Vale encolher para um ponteiro.

## O que foi verificado no hash de conteúdo

Feito com servidor e cliente de verdade, não só por teste:

- Conteúdo igual → conecta (`welcome: id=3`).
- Item editado no banco **sem rebakear** → servidor loga
  `peer 1 has content 51dba41ef4f26f92, we have 5fa67847f2af7dfb` e rejeita, e o
  cliente mostra `server rejected us: content mismatch: re-run game_bake`.
- O `fnv1a_64` bate com os vetores de referência publicados, que é o que garante o
  mesmo digest no Windows.

Ao fazer isso apareceu um bug pré-existente: o cliente só via "disconnected", sem
motivo, porque o `enet_peer_disconnect` corria com o pacote de reject. Trocado por
`enet_peer_disconnect_later`. Isso valia para "protocol version mismatch" também —
qualquer um que já tenha conectado com versão errada viu esse silêncio.

## O que NÃO foi verificado (e por quê)

- **Editar item pelo teclado, ponta a ponta.** `SDL_VIDEODRIVER=dummy` não entrega
  teclas. O formulário e o seletor foram verificados por screenshot; os caminhos de
  banco (`save_item`, `next_free_item_id`, `retire_item`, bake) e o writer de
  `atlas.txt` (9 casos, 50 asserções) por teste unitário; e o laço completo
  "criar item → vincular sprite → aparecer na paleta" foi exercitado pelo
  `--bind-sprite`, que é a mesma função que o clique chama.
- **`atlas.png` falhou de carregar uma vez** ("present but failed to load; falling
  back to procedural art") e eu **não reproduzi** em 3 tentativas seguidas. O
  arquivo é um PNG normal (8-bit RGBA, sem interlace, chunks padrão). Se voltar a
  acontecer, suspeitar do backend stb do SDL_image; sem mais evidência não dá para
  afirmar nada.

## Invariantes que esta trilha adicionou (não quebre de passagem)

- **O cliente não abre banco.** No Android os assets estão dentro do pacote e não
  são arquivos; SQLite quer caminho real. O `check-layering.sh` reprova
  `<sqlite3.h>` e link de `sqlite3`/`game_store` em `src/client/` e
  `src/platform/`. As duas regras foram testadas disparando de verdade.
- **`server-only` linka SQLite e isso está certo.** O invariante do preset é "sem
  SDL, sem lib gráfica", não "sem dependência".
- **Id de item é contrato.** Nunca reciclado; `retired_item_ids` guarda os
  aposentados (inclusive o 200 do ator, reservado) e `next_free_item_id` tira o
  máximo das duas tabelas.
- **O caminho do `atlas.txt` não é configurável por flag.** O `Tileset::load` lê via
  `platform::vfs` a partir do asset root; um writer apontado para outro lugar
  editaria um arquivo enquanto o editor mostra outro. Eu tentei adicionar `--atlas`
  e removi por isso.
- **SQLite é pinado por SHA3-256 do zip oficial**, não por SHA de commit — não é
  desenvolvido no GitHub e o amalgamation é gerado. O `verify-deps.sh` re-baixa e
  re-hasheia.
- **Nada é aceito pela metade**: blob recusado não deixa registry parcial, migração
  falha não deixa schema meio-aplicado, transação sem `commit()` faz rollback.
