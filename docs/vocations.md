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
| 3 | Mago elemental | MAG | burst / mana | ativo (sem spell ainda) |
| 4 | Druida | DRU | cura / controle | ativo (sem spell ainda) |
| 5 | Ladino | LAD | furtivo / PvP | stub (id reservado) |
| 6 | Necromante | NEC | invocador | stub (id reservado) |

Id é contrato: save e Hello futuro referenciam por número — nunca recicle.

## Arquivos

| Peça | Onde |
|---|---|
| Dados | `assets/vocations.txt` |
| Tipo | `sim/vocation_type.hpp` (`VocationType`, `VocationRegistry`) |
| Parser | `sim/vocation_io.hpp` |
| Fallback | `sim::default_vocations()` |
| No ator | `CVocation` + `CProgress` (nível/xp/mana) — ainda sem persistência |

## O que ainda não faz

- Escolha na criação / Hello com `vocation_id`
- Colunas em `players.db` (`kPlayerSchemaVersion` bump)
- XP de kill → level up aplicando `hp_per_level` / `mana_per_level`
- Spells, skills por uso, restrição dura de arma
- UI de create-character do mockup Grimhold

Ordem sugerida: persistir `vocation_id` + kit inicial no spawn → XP/nível →
skills/spells.
