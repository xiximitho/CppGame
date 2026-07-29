# Jogo isométrico multiplayer

Protótipo de jogo isométrico tile-based em C++20, no estilo Tibia: mundo por
tiles, múltiplos andares, movimento passo-a-passo, um personagem por jogador e
servidor autoritativo.

O objetivo deste repositório neste estágio é ser uma **base arquitetural correta**,
não um jogo. O que já funciona de verdade está listado em [O que já roda](#o-que-já-roda);
o que ainda não existe está em [docs/roadmap.md](docs/roadmap.md), explicitamente.

---

## Rodando em qualquer máquina

Este é o ponto central do projeto: **nenhuma dependência precisa estar instalada
no sistema.** SDL3, SDL3_image, EnTT, glm, ENet e doctest são baixados pelo
próprio build, travados em commits exatos (SHA, não tag). Você só precisa de
compilador, CMake e Ninja.

### 1. Pré-requisitos (uma vez por máquina)

```bash
# Linux
./scripts/setup-linux.sh

# macOS
./scripts/setup-macos.sh

# Windows (PowerShell como administrador)
.\scripts\setup-windows.ps1
```

Os scripts detectam o gerenciador de pacotes, instalam compilador + CMake + Ninja
e, no Linux, os headers de desenvolvimento que o SDL3 precisa para compilar
(Wayland, X11, ALSA). Eles são idempotentes: rodar de novo não quebra nada.

### 2. Compilar e rodar

```bash
git clone <url-do-repo> game && cd game

cmake --preset debug      # primeira vez: baixa as dependências (~2-5 min)
cmake --build --preset debug
ctest --preset debug      # os testes devem passar antes de você confiar em nada

./build/debug/bin/game_client            # abre o mundo em modo solo
```

Um clone limpo roda sem passo de instalação de asset. A arte vem de um atlas PNG
(`assets/tilesets/atlas.png`, decodificado pelo SDL_image) e o mundo de um mapa em
texto (`assets/maps/dungeon.txt`); se qualquer um faltar, o cliente cai num atlas
**gerado em código** e num mapa **gerado por seed**. Um arquivo ausente nunca
quebra o build nem a execução.

### 3. Multiplayer local

```bash
./scripts/run-local.sh          # sobe o servidor e um cliente conectado nele
```

Ou manualmente, em dois terminais:

```bash
./build/debug/bin/game_server
./build/debug/bin/game_client --connect 127.0.0.1 --name felipe
```

Para conectar de outra máquina na mesma rede, use o IP do servidor e libere a
porta **UDP 7777**.

---

## Presets de build

| Preset | Para quê |
|---|---|
| `debug` | Dia a dia. Sem otimização, com símbolos. |
| `release` | O que você distribui. `RelWithDebInfo`, otimizado mas depurável. |
| `server-only` | Servidor headless. **Não precisa de SDL3 nem de nenhuma lib gráfica** — é isso que você instala numa VPS. |
| `asan` | Debug + AddressSanitizer + UBSan. Rode os testes aqui antes de mexer em netcode. |
| `ci` | O que a CI compila: release com `-Werror`. |

```bash
cmake --preset server-only && cmake --build --preset server-only
```

---

## O que já roda

- **Mundo isométrico 2:1** (tiles 64×32) com múltiplos andares. Por padrão o mundo
  é um **calabouço de pedra carregado de arquivo**; sem arquivo, cai num mapa
  gerado deterministicamente por seed — lagos, salas com portas, trilhas, árvores,
  plataforma elevada.
- **Catálogo de itens/tipos** (`sim::ItemTypeRegistry`): cada `TileId` carrega
  propriedades de jogo (bloqueia passo/visão, pegável, empilhável, peso). O
  `blocking` de um tile é **derivado** dessas flags, não escrito à mão. Puro,
  sem I/O — o servidor continua precisando só dos números. Ver
  [docs/content.md](docs/content.md).
- **Arte por atlas PNG**: sprites carregados de `assets/tilesets/atlas.png` via
  SDL_image e ligados a ids por `assets/tilesets/atlas.txt`, com fallback
  procedural. Trocar a arte não recompila nada. Ver [docs/sprites.md](docs/sprites.md).
- **Mapas em arquivo texto**: mundo autorado em `assets/maps/*.txt` (formato
  legível, editável à mão), lido pelo **mesmo parser puro** no cliente (via VFS,
  à prova de APK) e no servidor (via `<fstream>`, sem SDL) — solo e multiplayer
  não podem divergir. Ver [docs/maps.md](docs/maps.md).
- **Editor de mapa** (`game_editor`): editor isométrico clicável cuja paleta é
  montada dos ids do catálogo + sprites do atlas; pinta/apaga tiles com o mouse e
  salva de volta pro `.txt`.
- **Movimento tile-a-tile** com duração de passo, custo maior na diagonal, e a
  regra estrita de não cortar quina de parede.
- **Occupancy real**: dois atores não entram no mesmo tile, e o destino é
  reservado no início do passo.
- **Clique-para-mover com A\***: custos em ticks (diagonal custa mais, então a
  rota escolhida é a mais rápida e não a de menos tiles), respeitando a mesma
  regra de quina que o movimento. Quem planeja e anda é a **simulação** — no
  multiplayer, o servidor: o cliente só manda o tile de destino, porque ele tem
  apenas os chunks que recebeu e não poderia planejar sozinho.
- **Servidor autoritativo** headless a 30 Hz, com snapshots a 10 Hz limitados por
  área de interesse (23×17 tiles) e **streaming de mapa por chunks** de 16×16.
- **Bit packing próprio**: um pacote de input tem 6 bytes; um chunk de mapa,
  ~670 bytes.
- **Cliente** com batching por atlas (a cena inteira em 1 draw call), câmera
  suavizada, picking exato de tile, zoom, andares acima do jogador escondidos,
  andares abaixo escurecidos.
- **Screenshot headless**: `--screenshot arquivo.bmp` funciona com
  `SDL_VIDEODRIVER=dummy`, ou seja, numa máquina sem display. Serve para checagem
  visual em CI e para alguém te mandar exatamente o que está vendo.
- **Solo e multiplayer usam o mesmo código de render**, porque o modo solo roda a
  simulação de verdade e monta o mesmo `Snapshot` que o servidor mandaria.
- **81 casos de teste / ~47k asserções** cobrindo projeção, picking, regras de
  movimento, área de interesse, formato de rede (incluindo pacote truncado e id
  desconhecido), o catálogo de itens e o round-trip do formato de mapa. Passam
  também sob AddressSanitizer e UBSan (`--preset asan`).

### Ressalva importante

Os **andares superiores não são alcançáveis**: não existe escada nem rampa, e o
spawn é sempre no andar 0. Toda a infraestrutura multi-andar existe (projeção,
ordenação, ocultação de teto, streaming por andar) e é coberta por testes, mas
nunca foi vista funcionando na tela. É o item 3 do [roadmap](docs/roadmap.md) e é
pequeno.

## Controles

| Tecla | Ação |
|---|---|
| `WASD` / setas | andar |
| Clique / toque | andar até o tile (rota calculada com A\*) |
| Roda do mouse, `+` / `-` | zoom |
| `F2` | alterna esquema de teclas (relativo à tela ↔ alinhado ao grid) |
| `Esc` | sair |

```bash
# Roda sem display e grava um frame — útil em CI e em relatório de bug
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_client --solo --screenshot /tmp/shot.bmp
```

Diagnóstico (fps, draw calls, tick, tile sob o cursor) fica no **título da
janela** — HUD de verdade depende de Dear ImGui, que está no roadmap.

## Editor de mapa

```bash
./build/debug/bin/game_editor --map assets/maps/dungeon.txt   # rode da raiz do repo
```

Editor isométrico simples: a paleta é montada dos ids do catálogo de itens + os
sprites do atlas, então mostra exatamente o que dá para colocar. Clique esquerdo
coloca o brush, direito apaga; um fantasma do brush segue o cursor. Controles
completos em [docs/maps.md](docs/maps.md).

## Estrutura

```
src/core/      log e tempo. Sem SDL, sem rede.
src/sim/       estado autoritativo e regras. Sem SDL, sem sockets, sem I/O, sem relógio.
src/net/       formato de fio (bit packing) e transporte atrás de ITransport.
src/platform/  SDL: janela, caminhos, VFS de assets (à prova de APK do Android).
src/client/    render, input, câmera, sessão solo e sessão remota.
src/editor/    editor de mapa isométrico (game_editor). Tool client-side; reusa o render do cliente.
src/server/    main() headless. Não linka SDL.
tests/         doctest. Não linka SDL.
```

A direção das dependências é de mão única e a CI **falha** se `sim/` passar a
incluir SDL, socket ou `<fstream>` (`scripts/check-layering.sh`). O motivo dessa
regra está em [docs/architecture.md](docs/architecture.md).

## Documentação

- [docs/architecture.md](docs/architecture.md) — por que as camadas são assim, e o
  modelo de rede escolhido
- [docs/dependencies.md](docs/dependencies.md) — como as dependências são travadas,
  como atualizar, como buildar **offline**
- [docs/mobile.md](docs/mobile.md) — Android e iOS: o que já está preparado e o que
  falta
- [docs/roadmap.md](docs/roadmap.md) — o que não existe ainda, em ordem de
  importância
- [docs/content.md](docs/content.md) — plataforma de itens/tipos e o pipeline de
  conteúdo (SQLite → bake → blob)
- [docs/sprites.md](docs/sprites.md) — como a arte chega na tela e como vincular
  um sprite a um objeto
- [docs/maps.md](docs/maps.md) — formato de mapa texto, carregamento e o editor
  `game_editor`

## Configuração opcional

Crie `assets/client.cfg` para mudar padrões sem recompilar:

```ini
host=192.168.0.10:7777
name=felipe
zoom=2.5
width=1600
height=900
```

Ele é lido pelo VFS, o mesmo caminho que funciona dentro de um APK no Android.
