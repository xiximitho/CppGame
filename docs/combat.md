# Combate e vida

> Status: **em implementação**. Fases 1 (vida visível) e 2 (melee auto-attack)
> feitas; faltam 3 (data-driven) e 4 (feedback/loot). Decisões fechadas com o
> usuário em 2026-07-29: auto-attack por alvo (estilo Tibia), morte = respawn
> simples.

## Princípios (herdados do resto do projeto)

- **Autoritativo no `sim/`.** Ataque é uma ação validada pelo servidor, igual ao
  movimento. O cliente manda *intenção* (mira um alvo), nunca dano. Solo roda o
  mesmo `World`, então não diverge do multiplayer.
- **Por tick.** Cooldown de ataque, tempo de morte e respawn são em `Tick`, como
  a duração de passo. Nada lê relógio.
- **Determinístico.** Variância de dano usa `sim::Rng`, nunca `<random>`.
- **Estado, não evento, na rede.** O que o cliente precisa para desenhar (vida)
  viaja como estado no snapshot, então perda de pacote não custa nada. Feedback
  efêmero (números de dano) pode virar evento não-confiável depois.

## O que já existe

- `sim::CHealth {hp, max_hp}` no `components.hpp`.
- `spawn_actor` já dá `CHealth{100, 100}` a todo ator.
- `ActorState` já carrega `hp`/`max_hp`, `read_actor_state` os preenche, e o
  **protocolo já os serializa** (`write_ranged`/`read_ranged`). Ou seja, a vida já
  trafega fim-a-fim; faltava só o cliente desenhar.

## Fase 1 — vida visível (feita)

Barra de vida sobre cada ator no `world_render`, desenhada em espaço de mundo
(acompanha o ator) com o texel sólido do atlas (`Tileset::solid()`), cor
verde→vermelho pela fração `hp/max_hp`. Sem mudança de `sim/` nem de protocolo —
tudo já estava no fio. Hoje todos aparecem cheios; a barra passa a cair sozinha
quando a fase 2 aplicar dano.

## Fase 2 — melee auto-attack (feita)

Modelo Tibia: mira um alvo e bate a cada cooldown enquanto ele estiver no alcance.
Implementado como abaixo: `CTarget`/`CDead`/`CRespawn` em components, `C2S_Attack`
no protocolo (`kProtocolVersion` 2→3), `sim::update_combat` rodando depois de
`world.step()` no servidor e na `SoloSession`, `in_melee_range` em types.hpp, e no
cliente clicar num ator vira ataque (clicar no chão continua sendo andar).

- **Intenção:** `C2S_Attack { target_net_id }` (canal confiável). O handler no
  servidor grava um alvo; `0` limpa. A `SoloSession` faz o mesmo localmente.
- **Componente:** `CTarget { NetId target; Tick next_swing_tick; }` no ator
  atacante. Some quando não há alvo (mesma disciplina de `CWalk`/`CPathFollow`:
  ausência = "não atacando").
- **Sistema:** `sim::update_combat(world)` **todo tick, depois de `world.step()`**
  (mesma regra de ordenação de `update_path_followers`). Para cada `CTarget`:
  resolve o alvo; se sumiu, limpa; se está no alcance melee (tile adjacente,
  incluindo diagonal com a regra de quina, olhando pelo `facing`) e
  `tick >= next_swing_tick`, aplica dano e agenda o próximo swing
  (`next_swing_tick = tick + kAttackCooldownTicks`). Fora de alcance: não bate,
  mas mantém o alvo (o jogador anda até ele com o movimento normal).
- **Dano (fase 2, simples):** valor fixo (ex.: `kBaseMeleeDamage`) menos uma
  defesa base. Vira data-driven na fase 3.
- **Regra única de alcance:** um `sim::in_melee_range(map, from, to)` análogo a
  `can_traverse` — uma fonte só, para o servidor e (depois) o indicador de alcance
  do cliente não discordarem.

### Morte e respawn

`hp <= 0` → o ator morre. Jogador: vira "morto" e **respawna** no spawn após
`kRespawnTicks` (hp restaurado); monstro: despawna. Implementação mínima sem loot
nem penalidade — é a fase 4. O alvo de quem atacava é limpo quando o alvo morre.

Detalhe de estado: um jogador morto não pode andar nem atacar até respawnar. Um
`CDead { Tick respawn_tick }` marca isso; `update_combat` (ou um
`update_respawn`) o processa. O `CHealth.hp` chegar a 0 no snapshot é o que o
cliente usa para tocar a morte.

## Fase 3 — dano data-driven + equipamento (a fazer)

Amarra no catálogo de conteúdo (`docs/content.md`): `ItemType` ganha stats de
combate (dano, defesa, velocidade de ataque). Um ator tem itens equipados; os
stats efetivos saem da soma base + equipado. Isso é a razão de o catálogo existir
— combate **consome** os tipos, não redefine regras.

## Fase 4 — feedback e loot

- **Números de dano (feito):** `client::DamageFeed` mostra "-N" flutuante quando o
  `hp` de um ator **cai entre views** — derivado do estado que o snapshot já
  carrega, sem pacote novo (mesma razão de "snapshot perdido não custa nada"),
  então funciona igual em solo e rede. Desenhado em 7 segmentos com o texel
  `Tileset::solid()`, sobe e some. Puramente apresentação; o estado autoritativo
  segue no `hp`. Um evento explícito `S2C_Damage` só valeria se precisássemos de
  tipo de dano/crítico que o delta de hp não conta.
- **Corpo e loot na morte (a fazer):** solta itens no chão (precisa de
  itens-no-chão, outra peça de conteúdo), penalidade de exp — o design "hardcore"
  do Grimhold.

## Protocolo

- Fase 1 não muda o fio (vida já serializada) — `kProtocolVersion` fica **2**.
- Fase 2 adiciona `C2S_Attack` → **bump para 3** (o servidor rejeita cliente de
  versão diferente no Hello; é o que impede misparse silencioso).
- Fase 4 adiciona `S2C_Damage` → bump novamente quando chegar.

## Invariantes a não quebrar

- `update_combat` roda **depois** de `world.step()`, todo tick.
- Alcance de ataque numa função só, como `can_traverse` é para passo.
- Dano com variância só via `sim::Rng`.
- Nada de dano/morte decidido no cliente; ele só desenha `hp` e toca feedback.
