# Arquitetura

## A regra que sustenta tudo

```
core  <-  sim  <-  net  <-  server
  ^        ^        ^
  +--------+--------+----  platform  <-  client
```

`src/sim/` é o estado autoritativo e as regras do jogo. Ele **não pode**:

- incluir SDL
- abrir socket
- ler ou escrever arquivo
- ler o relógio do sistema

Isso é verificado por `scripts/check-layering.sh`, que roda na CI e falha o build.

### Por que isso importa tanto

Três coisas ficam possíveis só por causa dessa regra:

1. **O servidor compila sem lib gráfica.** O preset `server-only` gera um binário
   para VPS sem X11, sem Wayland, sem áudio. Se `sim/` incluísse SDL, você
   instalaria Mesa numa máquina headless para rodar um servidor de texto.

2. **Os testes rodam a simulação diretamente.** `tests/test_world.cpp` avança
   ticks e verifica regras sem abrir janela nem abrir porta. É por isso que dá
   para testar "diagonal não corta quina de parede" em três linhas.

3. **O modo solo não pode divergir do multiplayer.** `SoloSession` roda o mesmo
   `World`, no mesmo tick rate, e monta o mesmo `Snapshot` que o servidor
   mandaria. Se o modo solo lesse o `entt::registry` direto, o single-player
   ganharia comportamentos que o servidor não valida — e ninguém descobriria até
   testar em rede.

O item 3 é o que mais frequentemente dá errado em projetos assim. A regra é o que
impede.

## Movimento por tile: a decisão mais importante do netcode

O personagem não tem posição contínua. Ele tem um tile inteiro e, quando está se
movendo, um `CWalk { from, to, start_tick, end_tick }`.

O deslizamento suave que você vê na tela é o **cliente interpolando** entre dois
tiles inteiros (`sim::interpolate`).

Consequências, todas boas:

- Um ator em movimento na rede é "saindo do tile A, indo pro B, 78% do caminho".
  São poucos bytes e **não precisa de stream de correção**.
- Um snapshot perdido não custa nada. O próximo diz exatamente onde o passo está,
  não um delta que dependia do anterior.
- Snapshots podem ir a 10 Hz mesmo com simulação a 30 Hz, sem perder suavidade.
- Não existe erro de predição acumulado, porque não existe integração de
  velocidade.

É por isso que um jogo estilo Tibia é muito mais barato de colocar em rede do que
um action RPG com movimento livre.

## Modelo de rede

**Servidor autoritativo, sem predição no cliente, ainda.**

O cliente manda intenção (`C2S_Input`: uma direção), o servidor decide, o cliente
desenha o que recebe. A latência visível é o início de um passo — em LAN é
imperceptível, e é a linha de base honesta para medir antes de otimizar.

### Área de interesse

O servidor só conta a cada jogador sobre atores dentro de uma janela de 23×17 tiles
(`sim::kAoiHalfX/Y`) no mesmo andar. Sem isso, o mundo não pode ser maior do que
um pacote. É a diferença entre um mapa de teste e um mundo persistente.

### Streaming de mapa

O mapa vai por chunks de 16×16 tiles em canal confiável, conforme o jogador se
aproxima, e o servidor lembra o que já mandou por conexão (`Connection::sent_chunks`).
Chunks totalmente vazios são marcados como enviados **sem transmitir**, porque o
cliente já trata tile desconhecido como vazio — em andares esparsos isso elimina a
maior parte do tráfego.

### Canais

| Canal | Uso | Por quê |
|---|---|---|
| Confiável ordenado | login, chunks de mapa, chat | tem que chegar, uma vez |
| Não confiável sequenciado | snapshots | reenviar estado velho é pior que descartar |

O cliente também descarta snapshot com tick menor que o último visto — canal não
confiável pode entregar fora de ordem.

## Onde a abstração existe (e onde não existe)

Duas abstrações no projeto são intencionais. Ambas protegem uma troca que **vai**
acontecer:

**`net::ITransport`** — hoje é ENet. Quando precisar de DTLS, IPv6 e relay da
Steam, o GameNetworkingSockets entra como segunda implementação da mesma interface
e nenhum código de jogo muda. GNS não foi usado desde já porque ele traz protobuf
e OpenSSL, o que transformaria o primeiro build de 2 minutos em 20.

**`client::Renderer2D`** — hoje é SDL_Render, que é portátil e já faz batching mas
**não aceita shader customizado**. No dia que o jogo precisar de luz dinâmica,
palette swap, outline ou fog of war shaderizado, uma implementação em SDL_GPU entra
atrás da mesma interface (~10 funções). Começar em SDL_GPU custaria semanas antes
de ver o primeiro tile na tela.

Fora dessas duas, o código é direto de propósito. Não há interface para "sistema de
audio" nem "provedor de assets" porque não há uma segunda implementação em vista.

## Ordenação de desenho

Painter's algorithm com chave composta (`iso::depth_key`):

```
andar * 1e6  +  (tile_x + tile_y) * 100  +  camada
```

Andar domina de forma absoluta, depois profundidade na tela, depois camada
(chão → objeto → ator).

**Limite conhecido:** isso é correto para tiles e objetos de um tile só. Um objeto
de 2×2 tiles (um prédio) vai ordenar errado contra um ator ao lado dele. A solução
é ordenação topológica dos sprites, e o dia que aparecer o primeiro prédio grande é
o dia de implementar. Está documentado no `iso.hpp` como limite, não como bug
escondido.

## Determinismo

O projeto **não** exige simulação determinística, porque o modelo é servidor
autoritativo e não lockstep. `sim/` usa `float` livremente.

Mas há uma exceção deliberada: `sim::Rng` é um PCG escrito à mão em vez de
`<random>`. As distribuições da biblioteca padrão não são especificadas para
produzir a mesma sequência entre implementações, então um mapa gerado por seed
sairia diferente no Linux e no Windows. Geração de mundo por seed precisa ser
estável entre plataformas.

Se o jogo algum dia virar lockstep (não vai, com 1 unidade por jogador), `sim/`
teria que migrar para ponto fixo inteiro. É a razão pela qual essa camada está
isolada: a migração seria contida.

## O que o `sim/` não sabe

- que existe rede (`CPlayer::peer` é um número opaco)
- que existe tileset (ids de tile são só números; só `blocking` tem significado)
- que existe tela

Isso é o que permite o servidor não ter arquivo de definição de tileset nenhum.
