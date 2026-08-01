# Vocações de personagem (Grimhold)

Classes permanentes escolhidas na criação. Espelham o padrão de
[`monsters.txt`](monsters.md): arquivo de texto, parser puro em `sim/`, fallback
em `default_vocations()`, **sem** `content_hash` (stats só entram onde a
simulação roda).

## Vocações

| Id | Nome | Código | Papel | V1 |
|---|---|---|---|---|
| 1 | Cavaleiro | CAV | tank melee | ativo |
| 2 | Paladino | PAL | híbrido ranged | ativo |
| 3 | Mago elemental | MAG | Firebolt | ativo |
| 4 | Druida | DRU | Nature's Touch (cura) | ativo |
| 5 | Ladino | LAD | furtivo / PvP | stub (id reservado) |
| 6 | Necromante | NEC | invocador | stub (id reservado) |

Id é contrato: save e Hello futuro referenciam por número — nunca recicle.

## Armas por classe

Qualquer um pode *equipar* qualquer arma; o dano usa
`sim::vocation_weapon_percent` (cavaleiro ruim com arco, mago/druida ruins com
espada, etc.). Cajado = item `310` (ranged, alcance 3).

Kit inicial (`starter` em `vocations.txt`, ícones de
`assets/tibia_like/otsp_equipment_01.png` via `tools/import_otsp_items.py`):

| Classe | Equipado | Mochila |
|---|---|---|
| Cavaleiro | espada, escudo aço, elmo, plate, pernas, botas | health potion |
| Paladino | arco, túnica ranger, leather legs/boots, amuleto | quiver, health potion |
| Mago | wand azul, robe, chapéu, botas azuis, anel | mana potion |
| Druida | nature staff, leather armor, wolf hood, botas, amuleto lua | mana potion |

Ids 311–338. Poções ainda não têm uso (só ocupam a mochila).

## Spells (V1)

Hardcoded em `sim/spell.hpp` (não no content.db). Uma skill ativa por classe
jogável; tecla `1` na hotbar.

| Id | Nome | Classe | Tecla | Efeito |
|---|---|---|---|---|
| 1 | Bash | Cavaleiro | `1` | dano melee (alcance 1; precisa de alvo) |
| 2 | Blessing | Paladino | `1` | cura em si |
| 3 | Firebolt | Mago | `1` | dano à distância (precisa de alvo na battle list) |
| 4 | Nature's Touch | Druida | `1` | cura em si |

### Como escolher a classe

Ao abrir o cliente aparece a tela **Choose your vocation** (4 cards jogáveis).
Teclas `1`–`4` ou clique no card; `Enter` / botão **ENTER** confirma; `Esc` sai.

```bash
./build/debug/bin/game_client --solo
```

Para pular a tela (scripts / CI):

```bash
./build/debug/bin/game_client --solo --vocation mage
# --screenshot também pula o picker (usa knight, ou o --vocation se passado)
```

Aliases: `cavaleiro`/`cav`, `paladino`/`pal`, `mago`/`mag`, `druida`/`dru`, ou
`1`–`4`.

Em multiplayer o Hello (protocolo 8) leva o `vocation` escolhido; o servidor
aplica em personagem **novo**. Save antigo ainda não persiste a classe.

## Arquivos

| Peça | Onde |
|---|---|
| Dados | `assets/vocations.txt` |
| Tipo | `sim/vocation_type.hpp` |
| Parser | `sim/vocation_io.hpp` |
| Spells | `sim/spell.hpp` / `spell.cpp` |
| Armas | `sim/weapon.hpp` |
| No ator | `CVocation` + `CProgress` + `CSpellCooldown` |
| UI picker | `client/vocation_select_ui.hpp` |
| UI hotbar | `client/spell_hotbar_ui.hpp` |
| Fio | `C2S_Hello.vocation` + `C2S_CastSpell` (protocolo 8) |

## O que ainda não faz

- Persistência de `vocation_id` em `players.db`
- Mana / vocation no snapshot (remoto mostra hotbar pelo `--vocation` / picker local)
- XP → level up com `hp_per_level` / `mana_per_level`
- Nome do personagem / mundos do mockup Grimhold na mesma tela
- Mais de uma spell por classe / skill por uso
- Ladino e Necromante jogáveis (faltam stealth / summon)
