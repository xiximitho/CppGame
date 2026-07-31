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

## Loot estilo Tibia — inventário de mob e drops (PROPOSTA)

Pedido: inventário por mob como no Tibia, com **chance por item** na morte. O
`pendencias.md` já existia; esta seção é o desenho. Cookbook de mob continua em
[monsters.md](monsters.md); combate/loot antigo em [combat.md](combat.md) (fase 4
parcialmente superada no código).

### O que já existe (não partir do zero)

| Peça | Onde | Comportamento hoje |
|---|---|---|
| `CInventory` / `CEquipment` | `components.hpp` | Mesmas structs do jogador; mob **pode** carregar itens |
| `MonsterType::loot` | `monster_type.hpp` + `monsters.txt` | **Um** id; em `spawn_monster` vira `CInventory{{loot, 1}}` — drop **garantido** |
| Morte de mob | `World::apply_damage` | Despeja `CInventory` + `CEquipment` no tile, depois `despawn` |
| Chão | `World::ground_` / `drop_item` | Vários stacks por tile (merge se stackable) |
| Coleta | `World::step` | Andar **em cima** do pile pega tudo (auto-loot), só quem tem mochila |
| Solo | `SoloSession` | Preenche `WorldView::ground_items` — **só o primeiro** item do pile |
| Rede | `RemoteSession` | `ground_items` **vazio** (TODO conhecido) |
| RNG | `sim::Rng` | Obrigatório para rolls; **não** usar `<random>` |

Ou seja: o esqueleto de “mob tinha item → cai no chão → jogador pega” já roda nos
testes (`test_monsters`, `test_combat`). Falta **tabela com chance**, UX de **cadáver**
(estilo Tibia) e **sync em rede** do que ficou no chão.

### Tibia vs hoje (o que “igual ao Tibia” implica)

No Tibia, na prática:

1. **Loot não fica no inventário do mob em vida** — na morte rola uma **tabela**
   (várias linhas, cada uma com chance e quantidade máxima; rolls independentes).
2. O resultado vai para um **container de cadáver** no tile (não some direto na
   mochila ao pisar).
3. O jogador **abre o corpo** e move itens (mesma semântica de container/backpack).

Hoje o jogo faz (1) errado para Tibia — item garantido no spawn — e (2)/(3) simplificados:
pile no chão + auto-pickup ao chegar no tile. Dá para chegar no Tibia em fases sem
jogar fora `ground_` / `CInventory`.

### Formato de dado (recomendação)

Manter fora do `content.db` (como stats de mob): servidor e solo leem o mesmo arquivo,
**sem** entrar no `content_hash` — igual `monsters.txt` hoje.

**Opção A — bloco dentro de `monsters.txt`** (menos arquivos, parser único):

```
class 1 rato
  appearance   1
  hp           14
  ...
  loot 306 1 80000      # item_id  max_count  chance_per_million
  loot 302 1 12000
```

- Denominador fixo documentado (`kLootChanceScale = 1'000'000`), como OT.
- Chave `loot` **antiga** (`loot 306` só) vira atalho: `max_count=1`, chance=100%.
- Chave desconhecida continua erro fatal no parse (não ignorar typo de chance).

**Opção B — `assets/loot.txt` ou `assets/loot/<class_id>.txt`**

- `monsters.txt` fica só stats; tabela referencia `class_id`.
- Melhor quando a lista de drops crescer; exige um include ou segundo parse no boot.

**Opção C — tabela SQLite no `content.db`**

- Só vale se quiser UI no editor (`F2`/modo mob) e um só lugar de conteúdo.
- Custo: migração de schema, possivelmente **entrar no hash de conteúdo** (servidor
  lê DB), rebake obrigatório, e UI — o oposto do motivo de `monsters.txt` existir.

**Recomendação:** começar com **Opção A**; extrair para B quando alguma classe passar
de ~15 linhas de loot.

Modelo C++ alvo:

```cpp
struct LootEntry {
    ItemTypeId    item = kItemNone;
    std::uint16_t max_count = 1;
    std::uint32_t chance;  // de kLootChanceScale; 0 = nunca
};
// MonsterType: std::vector<LootEntry> loot_table;
// Remover o campo único `loot` depois da migração do txt + default_monsters().
```

### Mudanças em `sim/` (por fase)

#### Fase L1 — Tabela + roll na morte (mínimo jogável)

| O quê | Detalhe |
|---|---|
| `MonsterType` + parser | `loot_table`; compat com linha `loot` única |
| `spawn_monster` | **Parar** de encher `CInventory` só por causa da tabela (opcional: flag `carries_loot` se no futuro mob “segura” item visível) |
| `roll_monster_loot(spec, Rng)` | Função pura em `systems.hpp` ou `loot.hpp`; retorna `vector<ItemStack>` |
| `apply_damage` (mob) | Chama roll → `drop_item` para cada stack; **não** ler tabela do inventário pré-spawn |
| `default_monsters()` + `monsters.txt` | Mesmas entradas; rat/skeleton/ogro com chance 100% no item atual (comportamento igual ao shippado) |
| Testes | RNG fixo: 0% nunca cai, 100% sempre; dois rolls independentes; soma de chances > scale ok (Tibia permite) |

**RNG:** o roll usa o `Rng` do **dono do World** (servidor / `SoloSession`), não
`tick` sozinho — senão replay e debug ficam ruins. Passar `Rng&` para a função de
morte ou ter `World` guardar um gerador só para loot (decidir uma vez).

#### Fase L2 — Chão honesto (solo + rede)

| O quê | Detalhe |
|---|---|
| `GroundItemView` | Vários itens por tile ou “+N” / ícone de pile |
| `RemoteSession` | Enviar piles na AoI (chunk confiável, delta por tile, ou snapshot parcial) — **provavelmente bump de protocolo** |
| Servidor | AoI hoje é só atores; chão precisa de regra (tiles com pile dentro da janela) |

Sem L2, multiplayer continua “mato e não vejo drop”.

#### Fase L3 — Cadáver estilo Tibia (UX + sim)

| O quê | Detalhe |
|---|---|
| Entidade **cadáver** | Sem `CActor` (invisível à battle list) ou com `CCorpse { NetId?, duration }` + `CInventory` + `CPosition` |
| Morte | Roll preenche inventário do cadáver; tile continua **ocupado** até esvaziar ou expirar |
| Cliente | Clique no cadáver → painel loot (reutilizar metade de `inventory_ui`) |
| Coleta | **Desligar** auto-pickup ao pisar (ou só para piles “de cadáver”) — config ou regra fixa Tibia |
| Rede | Intenção `C2S_Loot` / mover stack cadáver → mochila (espelhar `equip` validado) |

#### Fase L4 — Autoria

| O quê | Detalhe |
|---|---|
| Editor | `F4` ou submodo: editar linhas `loot` da classe (hoje `MobMode` **não** escreve `monsters.txt`) |
| Validação | Item id existe no `content.db`; chance ≤ scale; log no boot se classe referencia item aposentado |

### O que **não** precisa mudar (de primeira)

- `content_hash` / `game_bake` — se loot ficar em `monsters.txt`.
- `kProtocolVersion` — só na Fase L2/L3 quando sincronizar chão/cadáver.
- `ItemType` — já tem stackable, pickable; loot só referencia ids.
- Spawners — continuam spawnando classe; loot é propriedade da classe.

### Ordem sugerida de implementação

```mermaid
flowchart LR
  L1[L1: loot_table + roll na morte]
  L2[L2: sync chão / render pile]
  L3[L3: cadáver + UI loot]
  L4[L4: editor]
  L1 --> L2
  L2 --> L3
  L3 --> L4
```

1. **L1** — jogável em solo com piles no chão (já desenha um ícone) e testes.
2. **L2** — multiplayer enxerga drops.
3. **L3** — sensação Tibia (corpo, sem auto-loot).
4. **L4** — conforto de autoria.

### Riscos / decisões abertas

- **Cadáver ocupa tile?** Tibia sim — combate em volta do corpo muda pathing; alinhar
  com `occupant` e `can_enter`.
- **Loot de jogador morto?** Fora de escopo até existir penalidade de morte; `CRespawn`
  hoje não dropa.
- **Empilhar rolls iguais:** `drop_item` já merge; roll pode emitir um stack com
  `count > 1` se um único entry sortear quantidade (extensão: `min_count`..`max_count`).
- **Documentação:** atualizar [monsters.md](monsters.md) e receita B3/B4 em
  [authoring.md](authoring.md) quando L1 landar.

---

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

---

## Anexo — conjunto de mapas autorados (2026-07-30)

Cinco mapas novos em `assets/maps/` (`floresta`, `vila`, `caverna`, `ilha`,
`torre`), gerados por `tools/gen_maps.py`, mais `--map` no cliente, no servidor e
(já existia) no editor. Detalhes e tabela em [maps.md](maps.md).

**Por que gerador:** tile bloqueante mal colocado sela corredor ou ilha um bolsão,
e o mapa carrega bonito com uma parte inalcançável para sempre. Toda colocação
bloqueante passa por `place_guarded` (desfaz se encolher a região alcançável),
`seal_pockets` fecha o que sobrou, e `validate` reconfere e sai com código 1. O
flood fill usa a mesma regra de quina de `sim::can_traverse`.

**Verificado:** os cinco carregam pelo cliente (`LOG_INFO loaded map ... (WxH)`),
`validate` diz "all reachable" nos cinco, screenshot headless de cada um
(`--map ... --screenshot`), build com `-DGAME_WERROR=ON` limpo, `ctest` e
`check-layering.sh` passando.

**Achado no caminho, corrigido:** `world_render.cpp` pintava o andar **de cima**
quase preto. Com o ator descoberto, `top_visible_floor` devolve `actor_floor + 1`,
`depth_below` fica `-1`, e `255 - (-1) * 45 = 300` estourava o `static_cast<uint8_t>`
para 44. `std::max(120, ...)` não protegia esse lado. Agora é `std::clamp(..., 120,
255)`. Só aparecia com mapa de mais de um andar sob céu aberto, e não havia nenhum
até `torre.txt`.

**Não verificado / não feito:**
- **Nenhum teste automatizado abre os `.txt` commitados.** A suíte é livre de
  filesystem de propósito (`tests/CMakeLists.txt`), então um typo num mapa
  commitado hoje só apareceria como fallback silencioso para o mapa procedural com
  `LOG_WARN`. O gerador cobre isso na geração, não na CI; um teste com o caminho do
  source tree via `target_compile_definitions` resolveria.
---

## Anexo — escadas e classes de mob (2026-07-30)

Duas features em cima do conjunto de mapas, na mesma sessão.

### Escada (`<` sobe, `>` desce; ids 103/104)

Flags de item novas (`StairsUp`/`StairsDown`), regra em `sim::World::apply_stairs`,
sprites novos no atlas, editáveis no modo item do editor (dois campos de flag) e
pintáveis na paleta do editor de mapa. A `torre.txt` liga os três andares.

**A armadilha, resolvida:** escada dispara na chegada **por passo**. Ser *colocado*
na escada de cima não conta, senão um par simétrico teletransporta o ator para
sempre. `tests/test_stairs.cpp` tem esse caso explícito, mais destino em rocha,
destino ocupado, sem andar acima, e rota descartada na troca de andar.

**Verificado:** 7 testes de unidade; `torre.txt` com os três andares 100% alcançáveis
segundo o `reachable3` do gerador (que reimplementa a mesma semântica, inclusive o
detalhe do "chegou por escada, pode andar"); sprite conferido em screenshot do
editor; servidor carregando `torre.txt` e cliente conectando com hash de conteúdo
igual depois do insert dos itens 103/104 + `game_bake`.

### Classes de mob (rato, esqueleto, ogro)

`sim::MonsterType` + `sim::default_monsters()`; `spawn_monster` copia os números da
classe para `CActor`/`CHealth`/`CCombat`/`CMonster`; `sim::update_monsters` substitui
o `update_wanderers` placeholder (deletado, como o CLAUDE.md pedia). Sprites por
`appearance` em linhas `mob` do `atlas.txt`. Spawn autorado por linha `monster` no
mapa — o que fez o `ParsedMap` ser propagado e, de brinde, o `spawn @` passar a valer.

**Decisão de escopo:** o catálogo de monstros é **C++**, não tabela do `content.db`.
Uma tabela exige migração de schema (v1→v2), seção nova no blob, contribuição no
`content_hash` e UI no modo item para valer a pena; enquanto isso não existe, seria
só um segundo lugar para olhar. É deliberadamente a mesma forma que
`build_default_registry()` tinha antes do banco, então mover depois é mecânico.

**Aggro é "não-mob", não "jogador"**: no solo o ator local não tem `CPlayer`, então
aggro por `CPlayer` daria comportamento diferente em single-player. Tem teste.

**Verificado:** 8 testes (stats por classe, as três classes de fato diferentes,
perseguição, leash, mob não caça mob, spawn autorado pulando o impossível, loot na
morte, classe inexistente); screenshot com as três classes em campo e combate
rolando; servidor colocando 14 mobs autorados na torre, inclusive nos andares 1 e 2.

**Achado no caminho, corrigido:** UV sem recuo de meio texel. Com o atlas em 256×440
(não potência de dois), a borda da faixa de chão caía um fio adiante e o NEAREST
amostrava a faixa do cursor dourado — **um pontinho amarelo na ponta de cada tile de
piso**. Ver `docs/sprites.md`.

### Catálogo em arquivo e spawners (mesma sessão, segunda rodada)

Pedido: "tem que poder configurar a velocidade de cada tipo de mob, estão todos
rápidos demais" e "acho legal ter spawns para colocar, assim fica tudo aleatório".

**O catálogo saiu do C++ e virou `assets/monsters.txt`** (`sim/monster_io.hpp`),
lido pelo mesmo parser puro nas duas bordas — servidor por `<fstream>`, cliente por
`platform::vfs`. Editar e rodar; sem build, sem rebake. **Não** foi para o
`content.db`: stat de monstro só é lido onde a simulação roda (servidor em rede,
cliente no solo), então não há nada aqui para o hash de conteúdo proteger — foi o que
tornou um arquivo de texto o peso certo em vez de migração de schema + seção de blob
+ UI. `sim::default_monsters()` continua existindo como **fallback** e tem que
concordar com o arquivo.

**Velocidades corrigidas.** `kDefaultStepTicks` é 9 (o jogador), e eu tinha posto
rato em 6 — mais rápido que o jogador. Agora 13 / 19 / 30 (rato / esqueleto / ogro),
todas acima de 9, com teste garantindo que **nenhuma** classe shippada corre mais que
o jogador.

**Spawners autorados** (`spawner x y z classe quantos raio segundos`) mantêm
população e repõem **um por vez**, com o relógio começando na morte — limpar um ninho
tem que ficar limpo. `--wanderers` caiu para **0** por padrão nas duas pontas: o mundo
deixou de ser aleatório. Os 6 mapas ganharam ninhos; `monster` continua sendo o mob
único que não volta (o ogro do topo da torre).

O `World` passou a carregar o `MonsterRegistry` (`World::monsters()`), porque spawner
spawna em **runtime** e precisa do catálogo em mão — antes ele era um singleton em
parâmetro default.

**Verificado:** 7 testes novos (população enche e para, ninho limpo fica limpo pelo
tempo do respawn, nunca coloca mob em rocha nem em tile ocupado, classe inexistente
não arma ninho, mob spawnado usa o catálogo do mundo e não o embutido, catálogo
shippado parseia, catálogo malformado é recusado inteiro em 7 formas de erro); solo e
servidor carregando o `monsters.txt` e os ninhos populando em jogo.

### O erro que eu cometi nesta rodada (e o que ele revelou)

Rodei `tools/gen_dungeon.py` para dar ninhos ao `dungeon.txt` e **sobrescrevi a
versão editada que estava na árvore** (não commitada). Restaurei a partir do conteúdo
que eu tinha lido no começo da sessão e conferi tile por tile: **259 dos 2240 tiles
diferiam em caminhabilidade**, ou seja a edição era real e teria sido perdida. Os
ninhos foram então colocados na geometria restaurada, não na gerada.

**Duas coisas ficam disso:**

1. **`assets/maps/dungeon.txt` não é reproduzível pelo `gen_dungeon.py`** — o mesmo
   caso do `atlas.png`. Está avisado em `docs/maps.md`. Rodar o gerador por cima
   descarta a edição.
2. **O `@` do `dungeon.txt` está num tile de água+parede** (alguém pintou por cima
   dele no editor). Isso expôs um furo no que eu tinha feito antes: eu honrava o
   spawn autorado checando só ocupação, não caminhabilidade — todo jogador novo
   nasceria dentro da rocha. Agora servidor e solo checam `is_walkable` e caem em
   `find_spawn_tile` com `LOG_WARN`. Visto disparando de verdade:
   `map spawn (29,26,0) is not usable; spawning at (8,30,0)`.

**Não feito:**

- **Editor não coloca mob nem ninho.** As linhas `monster`/`spawner` são preservadas
  num save (o editor guarda o que carregou e o writer regrava), mas não há UI para
  adicionar ou mover. O risco real — save esvaziar o mapa — não existe.
- **Loot com chances** — ver seção [Loot estilo Tibia](#loot-estilo-tibia--inventário-de-mob-e-drops-proposta) acima; hoje é um item garantido via `loot` → `CInventory` no spawn.
- **Sem ranged de mob**: `kind ranged` parseia e nenhuma classe usa.
- **Sem nome de classe em jogo**: o `<nome>` do `monsters.txt` é lido e descartado, o
  cliente não mostra nameplate.

### Battle list e perseguição (2026-07-30, terceira rodada)

Pedido: painel à direita estilo Tibia com os mobs ordenados por proximidade, clique
seguindo/atacando; e "alguns casos ele não está seguindo por inteiro, ficando no
caminho e deixando de atacar".

**O bug era arquitetural, não de ajuste.** Clicar num mob mandava
`request_move_to(tile_do_mob)` **uma vez** e depois `request_attack`. A rota era para
onde o mob estava; ele andava; o último passo era recusado (o tile do alvo está
ocupado); o seguidor desistia depois de 1s (`kPathBlockedGiveUpTicks`) e o ator ficava
plantado com um alvo fora de alcance. Do lado do mob, a minha "passada gulosa" a 2
tiles travava em quina de parede — o diagonal era recusado pela regra de quina e ele
repetia o mesmo passo para sempre.

Agora existe `CFollow` + `sim::update_chasers`: o cliente manda **quem**, nunca
**onde**, e a simulação replaneja quando o alvo muda de tile, para de andar quando ele
entra no alcance, e solta a perseguição quando o alvo morre. Mob e jogador usam o
mesmo sistema. Movimento manual (`cancel_path`) cancela a perseguição, senão o input
parecia ignorado.

**Segundo bug, achado no caminho:** `World::set_attack_target` zerava
`next_swing_tick` em toda chamada, e o `update_monsters` chama algumas vezes por
segundo para um mob engajado — **mob batia a cada 3 ticks em vez de a cada cooldown,
~10x o dano pretendido**. Agora re-emitir o mesmo alvo não faz nada (`request_follow`
tem a mesma guarda). Se o combate parecia brutal antes, era isso.

**O painel** (`client/battle_list.hpp`) é montado só do `WorldView`, o que dá a
semântica do Tibia de graça: ele não consegue listar o que o servidor não mandou (AoI
+ mesmo andar). `build_battle_list` é `inline` e puro justamente para ser testável sem
janela — 7 testes cobrindo ordem por distância (Chebyshev, a mesma medida do alcance),
empate por id para a linha não trocar debaixo do cursor, exclusão do próprio jogador,
exclusão de outro andar, view não pronta, e os campos que a linha desenha.

**Verificado:** 174 testes; painel em solo e **em rede** por screenshot (4 mobs,
ordenados, nomes vindos do `monsters.txt` do cliente, barras de vida); os 7 casos de
perseguição incluindo alvo em movimento e parede com volta só pelo norte.

**Não verificado:** o **clique** no painel. `SDL_VIDEODRIVER=dummy` não entrega mouse,
então o hit-test compartilha a geometria com o desenho (`row_top`, uma função só, para
não haver dois palpites da altura do título) mas nunca foi exercitado automaticamente
— mesmo limite do formulário do editor.

### Portal entre mapas — NÃO IMPLEMENTADO

Pedido junto com as duas acima e deixado para uma mudança própria, de propósito: é a
única das três que não cabe em `sim/` sozinha. O que ela exige, e por isso não entrou
de carona:

1. `sim::World` por mapa, com o servidor mantendo vários e cada conexão sabendo em
   qual está (hoje `main.cpp` tem exatamente um `world`).
2. Mensagem nova de reset de mapa + **bump de `kProtocolVersion`** para 7: o cliente
   cacheia o `TileMap` e o `sent_chunks` por conexão precisa ser invalidado no
   trânsito.
3. `characters.map_name` no save (bump de `kPlayerSchemaVersion`), senão relogar
   devolve o jogador ao mapa errado.
4. `SoloSession` também com vários mundos, ou solo e rede divergem.

O caminho de menor risco é fazer o trânsito nascer como fila em `World` (o mesmo
lugar onde a escada age), drenada por quem é dono do mundo — servidor ou
`SoloSession` —, porque `sim/` não pode carregar arquivo.

### Escada e seleção de mapa (2026-07-31, quarta rodada)

Pedido: "a escada não me parece estar funcionando"; seleção de arquivo no editor
("dizer qual mapa quero mexer"); e o mesmo para o cliente, por parâmetro.

**A escada funciona — o que faltava era como autorar uma.** Investigado nesta ordem,
e vale registrar porque a conclusão é contraintuitiva: `flags` de 103/104 no
`content.db` estão certas (64/128); os 7 testes de `test_stairs.cpp` passam; o par da
`torre.txt` está alinhado (`<` em 16,15,0 e `>` em 16,15,1); o servidor manda chunk de
**todos** os andares (`stream_chunks` itera `z`), então rede não perde o andar de
cima. Fechando o laço, um teste novo anda do spawn da torre até a escada com o mapa
real e confere a mudança de andar — passa (tick 201, `(16,15,0)` → `(16,15,1)`). E o
andar 1 renderiza: verificado por screenshot com o spawn movido para lá.

O que **não** funcionava era chegar a uma escada útil:

1. Dos seis mapas commitados, **só a `torre.txt` tem escada**. Os outros cinco são
   `size ... 1` — um andar. No `dungeon.txt`, que é o padrão de cliente, servidor e
   editor, não existe escada para funcionar.
2. O editor editava **só o andar 0** (`const int floor = 0;`) e não sabia criar
   andar. Então dava para pintar a escada e era impossível pintar o destino dela —
   e `apply_stairs` recusa em silêncio quando o destino não existe, que é
   indistinguível de "escada quebrada" para quem está pisando.

Feito: `PgUp`/`PgDn` trocam o andar editado, `Ctrl+PgUp` adiciona um andar em cima,
`RenderParams::floor_override` faz o renderer desenhar o andar pedido (o editor não
tem ator de onde derivar um), a câmera acompanha o `kFloorHeight`, e com a escada no
pincel a barra de baixo avisa em laranja quando o andar de destino não existe. Esse
aviso é o ponto: o silêncio da regra é correto, o silêncio da ferramenta não era.

**Seleção de mapa.** `F3` abre `editor::MapBrowser` (`src/editor/map_browser.hpp`):
lista os `.txt` do diretório do mapa atual, setas + `Enter`, `Esc` cancela, e com
alteração não salva o primeiro `Enter` só arma o aviso. Usa `<filesystem>` direto e
não `platform::vfs` — vfs não enumera, e o editor é ferramenta de desktop que escreve
na árvore de fontes; o cliente, que precisa ler de dentro do APK, continua recebendo
caminho. Abrir limpa o undo (snapshots de outro arquivo são pior que nenhum).

**`--map` nos dois, agora tolerante.** Cliente e editor aceitam nome curto
(`torre`), caminho relativo aos assets, e o caminho completo que o editor imprime.
Antes, um typo caía em "mapa procedural" (cliente) ou "tela em branco 48×32"
(editor), o que parece a flag ter sido ignorada — o cliente loga qual candidato
resolveu. No editor um nome que não existe continua sendo um mapa novo.

**Verificado:** 178 testes (4 novos em `test_shipped_maps.cpp`: os seis mapas
parseiam; toda escada autorada leva a tile onde se pode ficar de pé; salvar um mapa
shippado não muda tile, andar, spawn nem spawner — o caminho do `S` no editor, que é
o que mais assusta num editor multi-andar; e a caminhada da torre acima). Build com
`-DGAME_WERROR=ON` limpo, `check-layering.sh` ok. Screenshots headless: andar 1 da
torre no editor, a lista de mapas por cima da paleta, e o aviso de escada sem destino
no `dungeon.txt`.

**Não verificado:** o teclado do editor, de novo — `PgUp`/`PgDn`/`Ctrl+PgUp`/`F3`
foram alcançados por flag (`--floor`, `--brush`, `--map-browser`), que é o mesmo
compromisso do `--item-mode`: o estado é observável, o caminho tecla→estado não.
`with_extra_floor` e o `S` depois de adicionar andar só foram exercitados por
inspeção e pelo round-trip do writer.

**Achado de passagem, não consertado:** no renderer headless (software) a fonte perde
a última linha de pixels de cada glifo, então `2`, `3` e `5` viram `?`/`7` num
screenshot — "floor 1/2" lê como "floor 1/7", e os ids do modo item ("102") como
"10?". Suspeita: o inset de meio texel de `entry_from_pixels` sobre uma célula de 8
texels, amostrado em NEAREST. Pode ser artefato só do driver dummy; numa janela de
verdade vale conferir antes de mexer.

Nota de bancada da mesma rodada: o `content.bin` da árvore estava **velho** (16 itens
contra 19 no `content.db` — alguém criou um item no editor e não rebakeou), e em rede
isso aparece como `content mismatch: re-run game_bake` no login, que é o
comportamento correto. Rodado o `game_bake`. O `run-local.sh` ganhou `GAME_MAP`
porque quem precisa do mapa em rede é o **servidor** — sem isso ele sempre subia o
`dungeon.txt`, de um andar, e "escada não funciona" em rede era literalmente "não
existe escada neste mundo".

## Animação de sprite — PRONTA (esta sessão)

Pedido como "o Tibia tem animação, dá uma estudada como fazer" e implementado de ponta
a ponta. O fundo, as medidas das folhas e as receitas estão em
[animation.md](animation.md); aqui fica só o handoff.

**Onde a decisão foi tomada:** o frame vem de `ActorState::walk_progress`, que o
snapshot já carrega. Isso deixou `sim/`, o servidor e `kProtocolVersion` (6) intactos —
animação é apresentação — e dá de graça a propriedade que importa: a fase é *derivada*
da posição no passo, não acumulada num contador local, então snapshot perdido não
dessincroniza animação como não dessincroniza posição.

**O que foi tocado:**

| Peça | O quê |
|---|---|
| `client/animation.hpp` | `walk_frame` + `art_direction`, header puro, sem SDL |
| `client/tileset.*` | kind `mobstrip`, `MobSprites`, `actor(facing, appearance, frame)` |
| `client/world_render.cpp` | passa o frame; `battle_list.cpp` usa `SouthEast` (a câmera) |
| `editor/mob_mode.*` | modo `F4`: lista as classes, edita a tira, preview animado |
| `editor/atlas_meta.*` | writer da linha `mobstrip` (puro, testado) |
| `tools/import_otsp.py` | recorta a folha, tira o magenta, cola no atlas, escreve a linha |
| `assets/tilesets/atlas.*` | atlas 256×440 → **512×512**; aparências 2 e 3 animadas |
| `renderer2d.hpp` + `sdl_backend.cpp` | `SpriteCmd::rotation`, pivô no pé, 0 draw call extra |

**Verificado:** 186 testes (8 novos em `test_animation.cpp`, 5 em `test_atlas_meta.cpp`);
build com `-DGAME_WERROR=ON` limpo; `check-layering.sh` ok; screenshot do jogo em quatro
frames do mesmo passo mostrando a silhueta do fantasma mudando enquanto ele anda;
screenshot do modo mob nas três classes (rato estático, esqueleto e ogro animados) e do
seletor de tira — tudo em **1 draw call**.

**Não verificado, mesmo limite de sempre:** o clique e o teclado no formulário. O driver
dummy não entrega input, então `--mob`, `--mob-picker` e `--bind-mob` alcançam os
caminhos, e `--bind-mob` chama a mesma função que o clique chama, mas o input em si
segue território não testado.

**Segunda passada, depois de olhar na tela:** os mobs ficavam **tombados**. Não era
direção errada — as quatro direções da arte são a mesma silhueta inclinada, porque a
arte é desenhada para uma grade alinhada aos eixos da tela e esta é isométrica. Virou um
campo `tilt` (graus, opcional, no fim da linha `mobstrip`), com as duas aparências
importadas em **30°**, ajustável ao vivo no `F4`. O pivô é o pé do sprite; a barra de
vida não inclina e o retrato da battle list inclina.

**Escolhas que valem revisitar:**

1. **Esquerda e direita da arte podem estar trocadas.** As folhas OTSP têm dois blocos
   com rosto e dois sem, o que não mapeia em `costas/direita/frente/esquerda` sem uma
   escolha arbitrária; frente e costas estão certas. O knob é `--dir-order` no
   importador (padrão `2,3,0,1`), e o preview do `F4` é onde se vê.
2. **Um ciclo completo por passo.** Todo passo toca os 3 frames na mesma ordem, em vez de
   alternar perna como o Tibia faz avançando um contador contínuo. Para esta arte (3
   poses de um loop, não neutro + 2 balanços) está certo; se um dia a arte tiver poses
   assimétricas, a alternância sai da paridade de `tile.x + tile.y` — derivada, não
   contada, para não perder a propriedade acima.
3. **O jogador não anima.** As linhas `actor <dir>` são 8 direções de 1 frame. Falta
   arte, não código: um `actorstrip` (ou mover o jogador para uma aparência) é pequeno.
4. **Criatura de 64×64 não foi importada.** Importador e formato aceitam
   (`--cell 64x64 --order cols`); a tira de 12 células alargaria o atlas para 1024.
5. **`assets/tibia_like/otsp_equipment_01.png` está intocada** — 690 células de item
   contra 19 itens no `content.db`. Não é animação: é recortar, `item <id> ...` no
   atlas.txt e uma linha no banco. `--bind-sprite` já vincula em lote.
