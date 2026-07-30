# Inventário, equipamento e combate data-driven

Como itens equipados definem ataque/defesa, os tipos de arma (melee/ranged), e o
efeito de ataque que aparece na tela. É a **fase 3** do [combate](combat.md)
amarrada num inventário, e consome a plataforma de conteúdo ([content.md](content.md)):
armas e armaduras são `ItemType`s com stats, não regras novas.

## Camadas (mesma disciplina do resto)

- **`sim/` é a verdade.** Stats, equipar, dano e alcance vivem no `sim`, puros e
  por tick. O cliente só desenha e manda intenção.
- **Estado na rede** para o que persiste (vida no snapshot); **evento** só para o
  que é instantâneo e descartável (o efeito de ataque).

## O que um item carrega (`sim::ItemType`)

Além de `flags/weight`, um tipo ganhou:

| Campo | Para quê |
|---|---|
| `equippable` + `slot` | se é equipamento e em qual slot (Tibia: arma, escudo, elmo, corpo, pernas, botas, anel, amuleto) |
| `attack` | soma ao ataque de quem veste (armas) |
| `defense` | soma à defesa (armaduras) |
| `attack_kind` | `Melee` ou `Ranged` (armas) |
| `attack_range` | alcance em tiles (melee = 1, arco = 4) |
| `effect` | id do sprite de efeito que o cliente toca |

Itens iniciais em `build_default_registry`: espada (melee +12), arco (ranged +8,
alcance 4), escudo (+6 def), elmo/corpo/pernas/botas/anel/amuleto (defesa).

## Componentes (`sim/components.hpp`)

- `CInventory { vector<ItemStack> }` — mochila (lista de pilhas).
- `CEquipment { array<ItemTypeId, kEquipSlotCount> }` — um item por slot.

`sim::combat_stats(world, entity)` soma base + tudo equipado → ataque, defesa,
alcance e efeito efetivos. `update_combat` usa isso: alcance por arma
(`in_attack_range`), dano = `max(1, ataque - defesa)`, e registra um
`AttackEvent{from, to, effect}` por golpe.

## Efeito de ataque (por arma, via evento no fio)

O `sim` é puro, então não desenha nem envia: `update_combat` empilha `AttackEvent`
em `World`. Quem drena:

- **Servidor**: envia `S2C_Effect{from, to, effect}` (não confiável, filtrado por
  AoI) e limpa. `kProtocolVersion` foi a **4**.
- **Solo**: joga os eventos direto no `WorldView`/sessão, sem fio.

O cliente (`client::EffectFeed`) renderiza: **melee** = brilho que floresce e some
no alvo; **ranged** = projétil que viaja do atacante ao alvo. Os sprites de efeito
vêm do atlas (`effect 1`, `effect 2` em `atlas.txt`). Como é evento (não estado),
um pacote perdido só custa um efeito não mostrado — nunca dessincroniza o combate.

## Sprites de equipamento e o painel

Cada item tem um ícone 16×16 no atlas (`item <id> …` em `atlas.txt`;
`Tileset::icon(id)`). O painel de inventário (`client::inventory_ui`, tecla `I`)
mostra os slots de equipamento com o ícone do que está vestido e a mochila abaixo,
lendo de `WorldView::equipment/inventory`. É desenhado como overlay fixo (mesmo
truque de câmera-inversa do editor).

## O que já roda

- Stats/slots/tipos no `ItemType`; `CInventory`/`CEquipment`; `combat_stats`;
  dano data-driven e alcance ranged no `update_combat`; itens iniciais; jogador
  nasce equipado (espada + armadura, arco/escudo na mochila). Testado em
  `tests/test_combat.cpp`.
- `S2C_Effect` no fio (protocolo 4); servidor envia, remoto recebe.
- `EffectFeed` (brilho melee / projétil ranged) e o painel de inventário.

## Limitações atuais (próximos passos)

- **Equipar/desequipar interativo** (arrastar itens entre mochila e slots) ainda
  não existe: o kit inicial é fixo. Precisa de comandos `C2S` de equipar.
- **Inventário do jogador remoto** não vai pro fio ainda — o painel só tem dados
  no solo (`WorldView::equipment/inventory` fica vazio em rede). É um TODO
  marcado no `session`.
- **Paperdoll**: a arma/armadura equipada não aparece no boneco, só no painel.
- Sem itens no chão / loot / peso limitando a mochila.
