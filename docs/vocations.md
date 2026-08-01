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
espada, etc.). Cajado = item `310`.

## Spells (V1)

Hardcoded em `sim/spell.hpp` (não no content.db):

| Id | Nome | Classe | Tecla | Efeito |
|---|---|---|---|---|
| 1 | Firebolt | Mago | `1` | dano à distância (precisa de alvo na battle list) |
| 2 | Nature's Touch | Druida | `1` | cura em si |

Cliente: `--vocation mage|druid|knight|paladin`. Hotbar no rodapé (mana + slot).
Servidor: `--vocation` no spawn de personagem **novo**.

## Arquivos

| Peça | Onde |
|---|---|
| Dados | `assets/vocations.txt` |
| Tipo | `sim/vocation_type.hpp` |
| Parser | `sim/vocation_io.hpp` |
| Spells | `sim/spell.hpp` / `spell.cpp` |
| Armas | `sim/weapon.hpp` |
| No ator | `CVocation` + `CProgress` + `CSpellCooldown` |
| UI | `client/spell_hotbar_ui.hpp` |
| Fio | `C2S_CastSpell` (protocolo 7) |

## O que ainda não faz

- Persistência de `vocation_id` em `players.db`
- Mana / vocation no snapshot (remoto mostra hotbar pelo `--vocation` local)
- XP → level up com `hp_per_level` / `mana_per_level`
- UI de create-character do mockup Grimhold
- Mais spells / skill por uso
