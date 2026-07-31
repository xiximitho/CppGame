# Mapa de classes

Visão das classes e structs principais em `src/`, complementando
[architecture.md](architecture.md) (decisões) com nomes e relações. Diagramas em
Mermaid — GitHub e muitos editores renderizam nativamente.

## Módulos e executáveis

```mermaid
flowchart TB
  subgraph executáveis
    game_client[game_client]
    game_server[game_server]
    game_editor[game_editor]
    game_bake[game_bake]
  end

  core[core<br/>log, time, bitstream, hash]
  sim[sim<br/>World, ECS, mapa, combate]
  net[net<br/>ITransport, protocolo]
  platform[platform<br/>vfs, paths]
  store[store<br/>Db, SQLite]
  client_mod[client<br/>Session, render, UI]
  editor_mod[editor<br/>ItemMode, MobMode]

  game_client --> client_mod
  game_client --> platform
  client_mod --> sim
  client_mod --> net
  client_mod --> platform

  game_server --> sim
  game_server --> net
  game_server --> store
  game_server --> core

  game_editor --> store
  game_editor --> sim
  game_editor --> client_mod

  game_bake --> store
  game_bake --> sim

  sim --> core
  net --> sim
  net --> core
  store --> core
  platform --> core
```

| Módulo | Headers principais |
|--------|-------------------|
| `core/` | `log.hpp`, `time.hpp`, `bitstream.hpp`, `hash.hpp` |
| `sim/` | `world.hpp`, `components.hpp`, `systems.hpp`, `snapshot.hpp`, `tile_map.hpp`, `pathfind.hpp`, `item_type.hpp`, `monster_type.hpp` |
| `net/` | `transport.hpp`, `protocol.hpp` |
| `platform/` | `vfs.hpp`, `paths.hpp` |
| `client/` | `session.hpp`, `renderer2d.hpp`, `tileset.hpp`, `world_render.hpp`, `iso.hpp`, `animation.hpp` |
| `store/` | `db.hpp`, `content.hpp`, `players.hpp`, `schema.hpp` |
| `editor/` | `item_mode.hpp`, `mob_mode.hpp`, `map_browser.hpp`, `atlas_meta.hpp` |

## Simulação: `World` e mapa

```mermaid
classDiagram
  direction TB

  class World {
    +TileMap map
    +ItemTypeRegistry item_types
    +MonsterRegistry monsters
    +entt::registry registry
    +Tick tick()
    +request_walk()
    +request_move_to()
    +cancel_path()
    +request_follow()
    +set_attack_target()
    +step()
  }

  class TileMap {
    +at(TilePos)
    +is_walkable()
  }

  class Tile {
    +TileId ground
    +TileId object
    +bool blocking
  }

  class Pathfinder {
    +find(map, from, to, path)
  }

  class ItemTypeRegistry {
    +lookup(ItemTypeId)
  }

  class MonsterRegistry {
    +lookup(MonsterTypeId)
  }

  World *-- TileMap
  World *-- ItemTypeRegistry
  World *-- MonsterRegistry
  World *-- Pathfinder
  World o-- entt_registry : registry_
  TileMap *-- Tile
```

`World` também mantém mapas auxiliares: `by_net_id_`, `occupancy_`, `ground_`
(loot no chão), fila `attack_events_`.

## Componentes ECS (`sim/components.hpp`)

Entidades são `entt::entity` no `World::registry()`. Componentes são structs
planas; presença/ausência define estado (ex.: sem `CWalk` = parado).

```mermaid
classDiagram
  direction LR

  class CActor {
    NetId net_id
    appearance
    step_ticks
  }
  class CPosition {
    TilePos tile
    Direction facing
  }
  class CWalk {
    from, to
    start_tick, end_tick
  }
  class CHealth
  class CPlayer {
    peer, name
  }
  class CCombat
  class CMonster {
    type, home, aggro, leash
  }
  class CSpawner {
    children, timers
  }
  class CTarget
  class CFollow
  class CPathFollow
  class CInventory
  class CEquipment
  class CRespawn
  class CDead
```

| Entidade típica | Componentes |
|-----------------|-------------|
| Jogador (servidor) | `CActor`, `CPosition`, `CHealth`, `CPlayer`, `CInventory`, `CEquipment`, `CRespawn`; opcional `CWalk`, `CTarget`, `CFollow`, `CPathFollow`, `CDead` |
| Mob | `CActor`, `CPosition`, `CHealth`, `CCombat`, `CMonster`; opcional `CTarget`, `CFollow`, `CWalk`, `CPathFollow` |
| Spawner | só `CSpawner` (sem `CActor` — não entra em snapshot) |

## Sistemas (funções, não classes)

Ordem fixa após `World::step()` (servidor e `SoloSession`):

1. `update_spawners`
2. `update_monsters`
3. `update_chasers`
4. `update_path_followers`
5. `update_combat`

Outras entradas em `systems.hpp`: `spawn_monster`, `combat_stats`, helpers de
spawn do mapa.

## Rede e snapshot

```mermaid
classDiagram
  direction TB

  class ITransport {
    <<abstract>>
    +poll(Event)
    +send(peer, data, Channel)
    +disconnect()
    +connected()
  }

  class EnetTransport {
    <<implementation>>
  }

  ITransport <|-- EnetTransport

  class Snapshot {
    Tick tick
    vector ActorState
  }

  class ActorState {
    tile, facing
    walking, walk_progress
    appearance, hp
  }

  Snapshot *-- ActorState
```

Mensagens de fio (`protocol.hpp`): `HelloMsg`, `InputMsg`, `MoveToMsg`,
`AttackMsg`, `EquipMsg`, `WelcomeMsg`, `MapChunkMsg`, `EffectMsg`, etc.

`build_snapshot(world, center, out)` preenche `Snapshot` pela área de interesse.

## Cliente: sessão e render

```mermaid
classDiagram
  direction TB

  class Session {
    <<abstract>>
    +update()
    +request_walk()
    +request_move_to()
    +request_attack()
    +view() WorldView
    +drain_effects()
  }

  class SoloSession {
    -World world_
    -Rng rng_
  }

  class RemoteSession {
    -ITransport transport_
  }

  Session <|-- SoloSession
  Session <|-- RemoteSession

  class WorldView {
    TileMap map
    vector ActorState actors
    NetId local_id
    equipment, inventory
  }

  SoloSession ..> World
  SoloSession ..> Snapshot : build_snapshot
  SoloSession ..> WorldView
  RemoteSession ..> ITransport
  RemoteSession ..> WorldView
```

```mermaid
classDiagram
  direction TB

  class Renderer2D {
    <<abstract>>
    +submit(SpriteCmd)
    +end_frame()
  }

  class SdlRenderer2D {
    <<implementation>>
  }

  class Tileset {
    +load(renderer)
    +ground(id), +object(id)
    +mob(appearance, dir, frame)
  }

  Renderer2D <|-- SdlRenderer2D
  Tileset --> Renderer2D
```

Fábricas: `make_solo_session`, `make_remote_session`, `create_server` /
`create_client` (transporte).

Apresentação auxiliar: `EffectFeed`, `DamageFeed`, `InventoryUI`; funções em
`world_render.hpp`, `battle_list.hpp` (header-only).

## Servidor (`server/main.cpp`)

Não há classe `Server` pública — o loop vive em `main`. Structs locais:

| Struct | Conteúdo |
|--------|----------|
| `ServerWorld` | `sim::World` + spawn opcional + listas de `MonsterSpawn` / `SpawnerSpec` do parse |
| `Connection` | `PeerId`, `NetId`, nome, chunks já enviados (`sent_chunks`), `character_id` SQLite |

Persistência: `store::Db` + `CharacterSave` via `snapshot_character` /
`save_character`.

## Store

```mermaid
classDiagram
  class Db {
    +open(path)
    +prepare(sql) Stmt
  }
  class Stmt {
    +bind_*()
    +step()
  }
  Db *-- Stmt
  class CharacterSave
```

Cliente **não** linka `store/` — lê `content.bin` via `platform::vfs`.
Servidor e `game_editor` / `game_bake` usam SQLite.

## Editor

| Tipo | Papel |
|------|--------|
| `MapDoc`, `Brush` | Tiles por andar |
| `ItemMode` | Tipos no `content.db` |
| `MobMode`, `MobRow` | Preview de mob / atlas |
| `MapBrowser` | Escolha de mapa |
| `AtlasBinding`, `MobStrip` | Metadados de `atlas.txt` |

## Fluxo de dados: solo vs rede

```mermaid
flowchart LR
  subgraph solo
    Input1[Input]
    SS[SoloSession]
    W[World]
    Snap[build_snapshot]
    WV1[WorldView]
    R1[Renderer2D]
    Input1 --> SS --> W --> Snap --> WV1 --> R1
  end

  subgraph multiplayer
    Input2[Input]
    RS[RemoteSession]
    TR[ITransport]
    SRV[Servidor + World]
    WV2[WorldView]
    R2[Renderer2D]
    Input2 --> RS
    RS <-->|C2S / S2C| TR <--> SRV
    RS --> WV2 --> R2
  end
```

## Fluxo: clique para atacar um mob

O cliente manda **quem**, não **onde**. Perseguir e bater ficam no servidor (ou no
`World` no solo).

```mermaid
sequenceDiagram
  participant UI as Cliente UI
  participant RS as Session
  participant NET as ITransport / solo
  participant SRV as Servidor ou SoloSession
  participant W as World
  participant SYS as update_chasers / update_combat

  UI->>RS: request_attack(target NetId)
  alt multiplayer
    RS->>NET: C2S_Attack
    NET->>SRV: handler
  end
  SRV->>W: set_attack_target(attacker, target)
  SRV->>W: request_follow(attacker, target)
  Note over W: cancel_path implícito no input manual;<br/>attack handler não cancela rota sozinho —<br/>follow + chasers replanejam

  loop cada tick
    W->>W: step()
    SYS->>W: update_monsters (mobs)
    SYS->>W: update_chasers (CFollow → request_move_to)
    SYS->>W: update_path_followers
    SYS->>W: update_combat (CTarget, alcance, dano)
  end

  SRV-->>NET: Snapshot + S2C_Effect
  NET-->>RS: WorldView + drain_effects
  RS-->>UI: battle list, animação, EffectFeed
```

Invariantes relevantes (detalhes em `CLAUDE.md`):

- `set_attack_target` com o **mesmo** alvo não reseta cooldown.
- `World::cancel_path` cancela perseguição (`CFollow`) — input manual retoma controle.
- `update_chasers` roda **antes** de `update_path_followers` no mesmo tick.

## Fluxo: criar um item (tipo novo)

Um `ItemTypeId` junta **três camadas** (ver [authoring.md](authoring.md)): arte
(`atlas`), regras (`content.db`) e colocação no mapa. Só arte deixa o id visível
no cliente, mas a simulação trata como desconhecido até existir linha no banco.

Receita passo a passo: **Receita A** em [authoring.md](authoring.md).

```mermaid
flowchart TD
  subgraph arte["1 — Apresentação (cliente)"]
    A1[Pintar recorte em atlas.png]
    A2{Vincular id → célula}
    A3[atlas.txt: ground / object / icon]
    A1 --> A2
    A2 -->|F2, campo sprite, Enter, clique| ED_BIND[ItemMode::bind_sprite]
    A2 -->|--bind-sprite kind:id:col:row| CLI_BIND[game_editor headless]
    A2 -->|linha manual| A3
    ED_BIND --> A3
    CLI_BIND --> A3
    A3 --> TS[Tileset recarrega]
  end

  subgraph regras["2 — Regras de jogo (servidor + sim)"]
    B1[game_editor F2]
    B2[N: novo id na faixa do item selecionado]
    B3[Editar flags, stats, nome, sprite kind]
    B4[S: save + bake]
    B5[(content.db)]
    B6[content.bin]
    B1 --> B2 --> B3 --> B4
    B4 --> B5
    B4 --> B6
  end

  subgraph mundo["3 — Colocação (opcional)"]
    C1[Editor mapa ou maps/*.txt]
    C2[TileMap: ground/object ids]
    C1 --> C2
  end

  subgraph runtime["Boot / rede"]
    R1[Servidor: store → ItemTypeRegistry]
    R2[Cliente: content.bin → ItemTypeRegistry]
    R3[Hello: content_hash deve bater]
    R4[Parser de mapa deriva blocking das flags]
    B5 --> R1
    B6 --> R2
    R1 --> R3
    R2 --> R3
    B5 --> R4
    C2 --> R4
  end

  arte -.->|mesmo id numérico| regras
  regras --> mundo
```

| Etapa | Se pular… |
|-------|-----------|
| Só `atlas.txt` | Cliente desenha; servidor não conhece flags (bloqueio, loot, etc.) |
| Só `content.db` | Simulação sabe o tipo; cliente usa sprite ausente ou fallback |
| Mapa sem rebake após editar DB fora do editor | Cliente rejeitado: `content mismatch` — rodar `game_bake` ou `S` no editor |
| Id reciclado | Mapas e clientes antigos quebram — usar `N` no editor, `shift+del` para aposentar |

Código: `ItemMode::save` / `bake` em `src/editor/item_mode.cpp`; carga no cliente
em `client::load_item_catalogue()`; no servidor em `store/content.hpp`.

---

## Fluxo: criar um mob (classe nova + aparecer no mundo)

Mob = **classe** (`monsters.txt`, simulação) + **aparência** (`appearance` →
`mobstrip` no `atlas.txt`, cliente) + **spawn no mapa**. O modo **F4** só grava
`atlas.txt`; **não** edita `monsters.txt`.

```mermaid
flowchart TD
  subgraph ids["0 — Contratos"]
    I1[Novo class id + appearance id]
    I2[Opcional: constante em monster_type.hpp]
    I1 --> I2
  end

  subgraph sim["1 — Classe (simulação)"]
    M1[Bloco class em assets/monsters.txt]
    M2[Espelhar em default_monsters se quiser fallback igual]
    M3[parse_monster_catalogue → MonsterRegistry]
    M1 --> M3
    M2 -.-> M3
  end

  subgraph arte["2 — Sprites (cliente)"]
    P1[Pixels no atlas: gen_placeholder --patch ou import_otsp.py]
    P2[F4 MobMode: célula, dirs, frames, tilt]
    P3[S: upsert mobstrip em atlas.txt]
    P1 --> P2 --> P3
    P3 --> TS[Tileset::mob appearance]
  end

  subgraph mapa["3 — População no mundo"]
    L1{Quantos e como?}
    L2[monster x y z class — único, não repõe]
    L3[spawner x y z class max raio seg — ninho]
    L1 --> L2
    L1 --> L3
  end

  subgraph boot["Arranque do World"]
    W1[parse_text_map → MonsterSpawn + SpawnerSpec]
    W2[spawn_authored_monsters]
    W3[create_spawners entidades CSpawner]
    W4[World construído com MonsterRegistry]
    W1 --> W2
    W1 --> W3
    M3 --> W4
    W4 --> W2
    W4 --> W3
  end

  subgraph tick["Runtime (cada tick)"]
    T1[update_spawners → spawn_monster]
    T2[Copia stats da classe → CActor, CHealth, CCombat, CMonster]
    T3[Snapshot leva appearance + hp]
    T4[Cliente: nome em monsters.txt + sprite por appearance]
    T1 --> T2 --> T3 --> T4
  end

  ids --> sim
  ids --> arte
  sim -->|appearance N| arte
  mapa --> boot
  boot --> tick
```

**Só balancear** uma classe existente: editar `monsters.txt` e rodar — sem bake,
sem build. Se o parse falhar, cai em `default_monsters()` e o log explica.

**Classe nova mínima:**

1. Entrada `class` em `monsters.txt` (hp, attack, `step_ticks`, aggro, `appearance`, loot…).
2. Linha `mobstrip` (ou `mob`) com o mesmo `appearance`.
3. Linha `monster` ou `spawner` no mapa (ou `--wanderers` só para teste rápido).

Receitas: **B3–B5** em [authoring.md](authoring.md); animação em [animation.md](animation.md).

| Arte | Simulação | Mapa |
|------|-----------|------|
| `appearance` no atlas | `class id` em monsters.txt | referencia **class id**, não appearance |
| F4 → `MobMode::save` | editar texto + reiniciar servidor/cliente | editor ainda não coloca mob/ninho na UI — `.txt` ou gerador |

Código: `spawn_monster` / `update_spawners` em `systems.hpp`; parse em
`monster_io.cpp`; bind em `mob_mode.cpp`; spawn do mapa em `map_io` + boot do
servidor/solo.

## Onde ler o código

| Conceito | Arquivo |
|----------|---------|
| API da simulação | `src/sim/include/sim/world.hpp` |
| Componentes | `src/sim/include/sim/components.hpp` |
| Tick loop dos sistemas | `src/sim/include/sim/systems.hpp` |
| Contrato do cliente | `src/client/include/client/session.hpp` |
| Solo | `src/client/src/solo_session.cpp` |
| Rede | `src/client/src/remote_session.cpp` |
| Servidor | `src/server/main.cpp` |
| Protocolo | `src/net/include/net/protocol.hpp` |
