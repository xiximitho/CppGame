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
| A | Hash de conteúdo no Hello + `kProtocolVersion` 6 | `d6d403b` |
| B | Persistência de jogador (`players.db`) | este commit |

**O laço completo funciona.** `./build/debug/bin/game_editor` → `F2` → `N` cria
item → setas editam campos → campo `sprite` + `enter` escolhe o recorte no atlas →
`S` salva. O `S` grava no banco **e** regera o `assets/content.bin`; o binding
reescreve o `atlas.txt` e recarrega o tileset na hora. Nenhum passo de terminal,
nenhum recompilar.

Servidor lê `content.db` direto; cliente lê `content.bin`.

## Pendências, em ordem de dependência

### A. Autenticação  (a pendência que sobrou de verdade)

`net::HelloMsg` carrega `name` e **nenhuma credencial**, então hoje o nome É a
identidade: qualquer um digita o nome de outro e recebe o personagem dele. Aceitável
num scaffold em rede confiável e não aceitável fora disso.

Isso foi deixado de fora de propósito, não por esquecimento: é uma decisão de
produto (conta separada do personagem? e-mail? recuperação?) e de segurança (KDF de
verdade — argon2/scrypt/bcrypt, nunca um hash cru), e envolve mudança de protocolo.
Não é coisa para contrabandear junto com o formato de save.

O que já está preparado: a tabela `accounts` existe separada de `characters`, com
`account_id` como FK, então "várias contas, vários personagens" é mudança de query e
não migração. Falta a coluna de credencial e o campo no Hello.

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
- **Um personagem por conta.** O schema permite vários (`characters.account_id`), a
  query não: `save_character` usa o nome do personagem como nome da conta. Crescer
  para seleção de personagem é mudança de query.
- **Nenhum stat além de hp.** Sem nível, sem experiência, sem skill. Quando
  existirem, são colunas novas em `characters` e um bump de `kPlayerSchemaVersion`.
- **`docs/authoring.md`** tem uma tabela de "ids em uso" que agora é redundante com
  o banco — o editor mostra a lista real. Vale encolher para um ponteiro.

## O que foi verificado na persistência

Com servidor e cliente de verdade:

- Primeiro login → personagem criado com o kit inicial e salvo no logout (espada no
  slot 0, armadura no slot 3, arco e escudo na mochila).
- Restart → `'felipe' restored at (18,21,0) with 42/140 hp`, com escudo equipado e
  3 anéis na mochila sobrevivendo ao ciclo salvar/carregar/re-salvar.
- Posição salva que virou inválida → `saved at (0,0,0), which is not usable now;
  spawning fresh`, e o resto do estado (hp, itens) ainda é restaurado.
- 9 testes unitários cobrindo round-trip, update em vez de duplicar, desequipar
  virando ausência de linha, isolamento entre dois personagens, facing corrompido, e
  `ON DELETE CASCADE` (que só funciona porque `store::Db` liga foreign keys).

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
- ~~**`atlas.png` falhou de carregar uma vez**~~ — **resolvido**, e não era o
  SDL_image. Era bug meu: o `bind_sprite` chamava o recarregamento do tileset
  enquanto o `ofstream` do `atlas.txt` ainda estava aberto, então o
  `Tileset::load` lia um arquivo truncado, o parse falhava e caía na arte
  procedural — o sintoma visível era "os sprites ficaram escuros ao trocar o
  tile". Corrigido fechando o stream explicitamente antes do reload.
  **A lição:** minha verificação olhava o CONTEÚDO DO ARQUIVO depois do processo
  sair, e não o recarregamento em processo. O arquivo estava sempre certo; o que
  estava errado era o que o reload via.

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
