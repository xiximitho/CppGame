# Animação de sprite

Como um mob (ou o jogador) ganha frames de caminhada: o que a arte tem que ter, onde
o vínculo mora, e por que a fase da animação é derivada e não contada.

## A ideia toda, em três linhas

O frame vem do **progresso do passo**, não de um relógio. `sim::ActorState` já carrega
`walking` e `walk_progress` (0..255), então:

- **nada em `sim/` mudou**, nada no servidor, e `net::kProtocolVersion` continua 6;
- um snapshot perdido custa à animação exatamente o que custa à posição: nada;
- não existe contador por ator no cliente para dessincronizar.

`client::anim::walk_frame(walking, progress, frames)` é a função inteira
(`src/client/include/client/animation.hpp`), e `tests/test_animation.cpp` a cobre.

## O formato: uma linha por criatura

`assets/tilesets/atlas.txt`:

```
# mobstrip <appearance> <x> <y> <cell_w> <cell_h> <dirs> <frames> <ox> <oy> [tilt]
mobstrip    2   0    448  32  32  4  3  -16  -16  30
```

As células ficam em **uma fileira** do atlas a partir de `(x, y)`, **direção-maior**:
célula `dir * frames + frame`. Uma linha em vez de doze porque um bloco 4×3 escrito
longhand são doze linhas que precisam concordar entre si e nada confere que concordam.

O kind antigo, `mob <appearance> <dir> ...` (uma linha por direção, sem animação),
continua funcionando — é o que a aparência 1 (rato) usa hoje, de propósito, para o
caminho estático continuar exercitado. **Os dois não convivem para a mesma
aparência**: eles são lidos na ordem do arquivo, então quem vem depois ganha. O
importador e o editor removem as linhas `mob` da aparência que passam a ter
`mobstrip`; `tests/test_atlas_meta.cpp` cobre isso.

`tilt` é opcional e vem por último, em **graus** — uma linha escrita antes de a
inclinação existir lê como 0 (em pé) em vez de falhar o parse. O porquê está na seção
[A inclinação](#a-inclinação) abaixo.

`origin` é o de sempre (`docs/sprites.md`): deslocamento do **vértice de cima** do tile
até o canto superior-esquerdo do sprite, e para um mob isso é `(-w/2, 16 - h)`, que põe
os pés no centro do tile. Errar aqui não é sutil: a barra de vida pendura no topo do
sprite, então uma origin errada faz a barra flutuar também.

## 8 direções de grid, 4 de arte

`sim::Direction` é espaço de **grid**. Na projeção 2:1 são as **diagonais** de grid que
apontam nos eixos da tela: `NorthWest` é cima, `NorthEast` direita, `SouthEast` baixo,
`SouthWest` esquerda (é o que `tests/test_input.cpp` deriva da própria projeção).

Arte estilo Tibia tem 4 direções, desenhadas em espaço de **tela**: costas, direita,
frente, esquerda — nessa ordem, que é a ordem de `dirs 4` no atlas. Então as quatro
direções de arte **são** as quatro diagonais de grid, e cada cardinal de grid cai
exatamente no meio de duas — empate, desempatado no sentido horário. Resultado: duas
direções de grid por coluna de arte, distribuição uniforme, e o comportamento lê certo
(andar para cima na tela mostra as costas; para baixo, a frente).

Isso vive em `anim::art_direction`, um lugar só, e o teste confere contra a projeção —
não contra a tabela.

## As folhas em `assets/tibia_like/`

Medido, não chutado (procedência e licença em `assets/tibia_like/CREDITS.md`):

| | `otsp_creatures_03.png` | `otsp_equipment_01.png` |
|---|---|---|
| Tamanho | 512×1664 | 512×1600 |
| Canal alfa | **não tem** | não tem |
| Transparência | color key `#FF00FF` (88,8%) | idem (68,1%) |
| Células 32px ocupadas | 463 de 832 | 690 de 800 |

Três coisas que valem saber antes de recortar:

1. **As duas têm watermark** — logo no canto superior direito, texto de licença no
   rodapé. Ocupam célula da grade e não são sprite.
2. **A unidade é 32×32**, e criatura grande ocupa 64×64 (2×2 unidades).
3. **O empacotamento não é uniforme.** As criaturas de 32px correm ao longo das linhas
   (12 células consecutivas na ordem de leitura); as de 64px descem em coluna. E há
   linhas com 14 células ocupadas em vez de 12. Por isso o importador recebe *célula
   inicial + contagem* em vez de detectar blocos: detecção erraria em silêncio.

**Ordem das direções na folha ≠ ordem do atlas.** Nas criaturas que conferi, os dois
primeiros blocos mostram o rosto e os dois últimos as costas, o que não é a ordem
`costas, direita, frente, esquerda` do atlas. O `--dir-order` do importador
(padrão `2,3,0,1`) reordena as células **fisicamente na hora de colar**, então o
formato do atlas fica simples e nada a jusante sabe que a folha tinha outra ideia de
qual lado é "longe da câmera".

Frente e costas são inequívocas; **esquerda e direita são cara ou coroa** numa criatura
sem rosto, e nestas folhas só dois dos quatro blocos têm rosto — então um dos lados
sempre fica sem. É esse o knob a virar quando um mob anda de lado errado, e o preview
do editor é onde se vê.

## A inclinação

⚠️ **Esta seção afirmava o contrário até 2026-08-01, e o contrário estava errado.** O
texto dizia: arte estilo Tibia é desenhada para uma grade alinhada aos eixos da tela,
numa isométrica a criatura "tomba", e ~30° a põe de pé — com 30 sendo o default do
importador. Foi medido e é falso:

- as células de `otsp_creatures_03.png` estão **de pé** na folha;
- o importador copia pixel a pixel (as bbox de conteúdo das 12 células do atlas batem
  uma a uma com as da folha, só permutadas pelo `--dir-order`);
- e como o pivô é o **pé** do sprite, 30° empurra um corpo de 32px uns **16px para o
  lado**: os pés ficam no tile e o corpo sai dele.

Ou seja, o `tilt` de 30° não corrigia inclinação nenhuma — ele criava o deslocamento
que parecia ser problema de ângulo de visão. O valor certo para estas folhas é **0**,
que é o novo default do `--tilt`.

O campo continua existindo, porque é barato e uma folha pode de fato trazer arte
tombada. Mas a suspeita, ao ver um mob desalinhado, deve começar pela **ancoragem**
(`origin_x`/`origin_y`) e não pela rotação.

Duas decisões dentro disso:

- **O pivô é o pé do sprite** (base do centro da célula), não o meio. A única coisa que
  um sprite girado precisa preservar é o contato com o chão: girar pelo meio desliza os
  pés para fora do tile pela metade da altura.
- **Não custa draw call.** O backend já emite quatro vértices por sprite via
  `SDL_RenderGeometry`, então um quad girado é o mesmo desenho. O caminho sem
  inclinação continua sem nenhuma conta: `rotation == 0` desvia do `sin`/`cos`, porque
  todo tile de chão da cena passa por ali.
- A amostragem continua `NEAREST`, então a borda de um sprite girado fica em degraus.
  É por isso que a inclinação é um knob de autoria por conjunto e não algo que o
  renderer aplica sozinho.

A barra de vida **não** inclina (é outro submit): barra torta lê como falha de render,
não como monstro inclinado. O retrato na battle list inclina, senão o painel mostra a
criatura tombada ao lado da mesma criatura de pé no mapa.

## Receita: importar uma criatura

```bash
# 1. Escolher o bloco na folha (célula inicial em células, não em pixels)
python3 tools/import_otsp.py --sheet assets/tibia_like/otsp_creatures_03.png \
    --at 0,47 --cell 32x32 --dirs 4 --frames 3 --appearance 2 --dry-run

# 2. Sem --dry-run: recorta, tira o magenta, cola numa faixa nova do atlas.png
#    e escreve a linha mobstrip no atlas.txt
python3 tools/import_otsp.py --sheet assets/tibia_like/otsp_creatures_03.png \
    --at 0,47 --cell 32x32 --appearance 2

# 3. Conferir a olho — e é aqui que se acerta direção e contagem de frames
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --mob 2 --screenshot /tmp/m.bmp
```

Reimportar a mesma aparência com a mesma geometria **reusa a faixa** que ela já tem: dá
para repetir enquanto se procura a criatura certa sem o atlas crescer 12 células por
tentativa. E o importador só usa `zlib` da stdlib — `Pillow`, de que o
`gen_placeholder_atlas.py` depende, não está instalado em toda máquina.

O atlas passou de 256×440 para **512×512** nesse processo: uma tira de 12 células de
32px são 384 px, que não cabem em 256. Isso não muda nada a jusante — todo `AtlasEntry`
tem uv calculada do tamanho real no load —, mas o `--patch` do
`gen_placeholder_atlas.py` foi ajustado para nunca estreitar o canvas, senão ele
cortaria as criaturas importadas fora.

A faixa nova é **alinhada a um múltiplo de `cell_h`**, e não simplesmente colada na
altura antiga. O seletor do editor põe uma grade de células sobre a folha inteira: uma
faixa começando em `y=440` com células de 32 é uma faixa em que nenhum clique
consegue pousar. Custa no máximo `cell_h - 1` linhas de atlas transparente.

## O editor: `F4`

`game_editor` → `F4` abre o modo mob (`src/editor/mob_mode.hpp`). Ele edita **só** a
metade de apresentação de uma classe: os números (hp, velocidade, aggro, loot) ficam em
`assets/monsters.txt`, onde mexer não precisa de ferramenta nenhuma.

```
PgUp/PgDn   troca de classe        up/down    troca de campo
left/right  muda o valor           enter      abre o seletor de célula
S           salva no atlas.txt     R          recarrega    F4  volta ao mapa
```

Campos: célula inicial, largura/altura da célula, direções (4 ou 8), frames, **tilt**
(±1°, ±8° com shift) e origin x/y. O painel da direita desenha **todos** os frames de **todas** as direções, com uma
célula animada pela mesma `anim::walk_frame` que o jogo usa — se o preview claudica, o
jogo claudica. É esse painel que pega tamanho de célula errado, contagem de frames
errada e direção trocada, que nenhuma figura estática mostra.

Abrir uma classe **estática** não mostra formulário vazio: o modo lê a primeira linha
`mob` dela e começa dali (8 direções, 1 frame), então virar animada é uma edição, não
um preenchimento. Salvar sem mexer em nada revincula a mesma arte.

Sem janela (o driver dummy não entrega teclado nem mouse):

```bash
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --mob 2 --screenshot /tmp/m.bmp
SDL_VIDEODRIVER=dummy ./build/debug/bin/game_editor --mob 1 --mob-picker --screenshot /tmp/p.bmp
# vincular em lote, sem clicar: appearance:cellx:celly[:dirs:frames:cellw:cellh]
./build/debug/bin/game_editor --bind-mob 2:0:14
```

## O que ainda não anima

- **O jogador.** As linhas `actor <dir>` são 8 direções de um frame. O caminho está
  pronto (basta um `actorstrip`, ou mover o jogador para uma aparência); o que falta é
  arte de jogador com frames.
- **Ataque, morte, animação em pé.** As folhas trazem 3 frames de caminhada e nada
  mais. Animação em pé precisaria de relógio (legítimo no cliente, proibido em `sim/`).
- **Criaturas de 64×64.** O importador aceita (`--cell 64x64 --order cols`) e o formato
  também; nenhuma foi importada ainda porque uma tira de 12 células de 64px alarga o
  atlas para 1024.
