# Monstros: classes, spawners e o que é dado

Duas coisas separadas, e a separação importa:

- **Classe de mob** = o que a criatura *é* (hp, dano, alcance, velocidade, aggro,
  sprite, loot). Vive em `assets/monsters.txt`.
- **Spawner** = *onde e quanto* dela existe no mundo. Vive no arquivo do mapa
  (`assets/maps/*.txt`), uma linha por ninho.

Nenhuma das duas exige recompilar. Regra de jogo (perseguir, atacar, respawnar) é
código; números e colocação são dado.

## `assets/monsters.txt` — as classes

```
class <id> <nome>          # id é CONTRATO; nome é só para humanos
  <chave> <valor>
```

| Chave | O que é |
|---|---|
| `appearance` | qual conjunto de sprites o cliente desenha (linha `mobstrip` ou `mob` do `atlas.txt`; `F4` no editor edita isso — ver [animation.md](animation.md)) |
| `hp` | vida máxima |
| `attack` / `defense` | somados por `sim::combat_stats` (mob não tem equipamento) |
| `kind` | `melee` ou `ranged` |
| `range` | alcance do ataque, em tiles |
| `effect` | id do efeito visual do golpe |
| **`step_ticks`** | **velocidade**: ticks por passo cardinal a 30 Hz |
| `aggro` | a que distância (tiles) ele te nota; 0 = nunca ataca |
| `leash` | o quanto se afasta de onde nasceu sem alvo |
| `loot` | tabela de drop na morte (implementação pendente; ver abaixo) |

Chave que falta fica no default do `MonsterType`. **Chave desconhecida é erro** — um
`speed 10` que fosse ignorado em silêncio seria um mob que mantém os números
antigos enquanto o autor jura que mudou.

### Tabela de loot (design fechado; código ainda legado)

Várias linhas por classe:

```
  loot <item_id> <max_count> <chance>
```

- `chance` de **`1_000_000`** (rolls independentes na morte).
- Só **`max_count`** no arquivo (sem `min_count`); quantidade ao dropar entre 1 e
  `max_count` quando o roll passa.
- Resultado num **cadáver fantasma** no tile (**não bloqueia** passo); coleta
  **manual** (sem auto-loot ao andar).
- Formato legado `loot 306` (só id) → `max_count=1`, chance 100%.

Fases de implementação: [pendencias.md](pendencias.md#loot-estilo-tibia--inventário-de-mob-e-drops).

### `step_ticks` é o parâmetro que você vai mexer

O jogador anda a `kDefaultStepTicks` = **9**. Então:

| `step_ticks` | Resultado |
|---|---|
| < 9 | mais rápido que o jogador — não dá para fugir |
| 9 | mesma velocidade |
| > 9 | mais lento; desengajar sempre funciona |

Os três que vêm no jogo estão em 13 (rato), 19 (esqueleto) e 30 (ogro) — todos mais
lentos que o jogador de propósito. Diagonal custa proporcionalmente mais; quem
calcula é o `sim`, não o arquivo.

### Quem lê, e por que não é o `content.db`

O servidor lê com `<fstream>` (não linka SDL); o cliente lê pelo `platform::vfs`
(no Android o arquivo está dentro do pacote). **Mesmo parser** nos dois
(`sim::parse_monster_catalogue`), então uma classe não pode significar duas coisas.

Arquivo ausente ou malformado → cai no catálogo embutido
(`sim::default_monsters()`) com `LOG_WARN` dizendo o motivo. Os dois têm que
concordar: um fallback que joga diferente transforma "o asset não carregou" em "o
jogo mudou".

Não está no `content.db` porque stat de monstro só é lido **onde a simulação roda** —
servidor em rede, cliente no solo. Um cliente conectado nunca usa a cópia dele, então
não há nada aqui para o hash de conteúdo proteger, e ajustar velocidade **não pede
rebake**. É isso que faz um arquivo de texto ser o peso certo para essa informação.
(Uma tabela no banco continua sendo o destino final se um dia houver UI de edição —
ver [pendencias.md](pendencias.md).)

## `spawner` — os ninhos

No arquivo do mapa:

```
spawner <x> <y> <z> <classe> <quantos> <raio> <segundos>
```

Mantém até `<quantos>` vivos daquela classe dentro de `<raio>` tiles do ponto;
quando um morre, o substituto aparece `<segundos>` depois. Um por vez, não refill
completo — limpar um ninho tem que ficar limpo por um tempo, senão não é um lugar
que você limpou, é uma torneira.

```
spawner 31 13 0 1 3 2 20      # 3 ratos, raio 2, 20s
spawner 44 22 0 3 1 2 100     # 1 ogro, raio 2, 100s
```

Um `monster <x> <y> <z> <classe>` continua existindo e é outra coisa: **um** mob que
não volta. Use para o encontro único (o ogro no topo da torre); use `spawner` para
população.

Detalhes de implementação que importam:

- O spawner é uma entidade **sem `CActor`** — invisível, não entra em snapshot, não
  existe para o cliente.
- Ele guarda os `NetId` dos filhos em vez de contar mobs por perto, então um mob que
  andou até o limite do leash ainda conta, e o do ninho vizinho não.
- Ele nunca coloca mob em rocha nem em tile ocupado: sorteia posição no raio e, se
  não achar em algumas tentativas, tenta de novo no tick seguinte.
- O relógio começa **na morte**, não no spawn.

## Comportamento (isso é código)

`sim::update_monsters`: persegue e ataca o **não-mob** mais próximo dentro do aggro,
com histerese de 1.5x; sem alvo, vagueia dentro do leash em volta de `home`. É
"não-mob" e não "jogador" porque no solo o ator local não tem `CPlayer` — aggro por
`CPlayer` daria comportamento diferente em single-player. Ver o CLAUDE.md.

Mob morto **não** volta por conta própria (não tem `CRespawn`, ao contrário do
jogador): quem repõe é o spawner.

## A battle list (painel da direita)

Lista os bichos visíveis, **mais próximo primeiro**, com sprite, nome da classe,
distância em tiles e barra de vida. Clicar numa linha ataca — e atacar implica
perseguir, então o personagem fecha a distância sozinho e continua fechando enquanto
o alvo anda. A linha do alvo atual ganha moldura dourada.

O painel fica na direita, abaixo do inventário quando ele está aberto. Ele é montado
só a partir do `WorldView`, ou seja do que o servidor mandou (AoI + mesmo andar):
não existe informação nova no cliente por causa dele.

Sair da lista acontece sozinho quando o bicho morre ou sai de vista.

### Perseguição (o que consertou "segue metade do caminho e para de atacar")

O cliente manda **quem**, nunca **onde**. Antes, clicar num mob mandava
`request_move_to(tile_do_mob)` uma vez: o mob andava, a rota acabava — ou o último
passo era recusado, porque o tile do alvo está ocupado — e o personagem ficava
parado com um alvo fora de alcance, sem bater.

Agora `CFollow` + `sim::update_chasers`:

- replaneja quando o alvo muda de tile (e no pior caso 4x por segundo);
- **para de andar** quando o alvo entra no alcance (1 tile para melee, 2 para o
  ogro) e deixa o `update_combat` bater;
- solta a perseguição quando o alvo morre ou desaparece;
- é cancelada por movimento manual — tecla ou clique no chão retomam o controle.

Mob usa exatamente o mesmo sistema. O que ele tinha antes era uma "passada gulosa"
na direção do alvo quando estava a 2 tiles, que travava em quina de parede: o
diagonal era recusado pela regra de quina, ele repetia o mesmo passo para sempre e
ficava plantado a 2 tiles sem atacar. `tests/test_chase.cpp` tem esse caso (parede
com volta só pelo norte) e o do alvo em movimento.

## Como testar uma mudança de balanceamento

```bash
# edite assets/monsters.txt e rode — sem build, sem rebake
./build/debug/bin/game_client --solo --map maps/caverna.txt

# só o que o mapa autora (padrão); --wanderers N espalha mobs aleatórios por cima
./build/debug/bin/game_server --map assets/maps/caverna.txt --wanderers 0
./build/debug/bin/game_server --monsters /tmp/experimento.txt
```

Um catálogo que não parseia aparece no log na primeira linha, então se o mob não
mudou, o motivo está lá.
