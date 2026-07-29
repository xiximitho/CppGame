# Android e iOS

## Status honesto

**O que está pronto:** as decisões estruturais que são caras de mudar depois.

**O que não está:** o build. Nunca foi executado, porque exige Android SDK/NDK ou
Xcode instalados. Trate as instruções abaixo como um caminho pesquisado, não como
um procedimento validado.

## O que já está resolvido

Estas são as coisas que, se você deixar para o mês 6, doem muito:

### Assets dentro do APK
No Android os assets vivem **dentro** do APK e não são arquivos no filesystem.
`std::ifstream` simplesmente não consegue abri-los. Todo acesso a asset no projeto
passa por `platform::vfs`, que usa `SDL_IOFromFile`/`SDL_LoadFile` — SDL roteia
para o asset manager do APK automaticamente.

Se você adicionar um `<fstream>` para carregar um tileset, vai funcionar no desktop
e falhar no device. Essa é a armadilha número um.

### Diretório de escrita
`platform::user_root()` usa `SDL_GetPrefPath`, que devolve o caminho correto por
plataforma. No Android e num app empacotado de desktop, o diretório de assets é
**read-only** — save nunca pode ir para lá.

### Input por toque
`SDL_EVENT_FINGER_DOWN` já é tratado em `main.cpp`, incluindo a diferença que pega
todo mundo: coordenadas de toque são **normalizadas** (0..1), não pixels como as do
mouse.

Não existe hover no touch, então a interação foi desenhada como "toque = passo na
direção do tile", não como adaptação de mouse.

### Escolha de biblioteca
SDL3 é a única opção que cobre Win/Linux/Mac/Android/iOS com o mesmo código,
incluindo ciclo de vida de app mobile. Essa decisão já está tomada e é o motivo de
não haver GLFW aqui.

### Alto DPI
A janela usa `SDL_WINDOW_HIGH_PIXEL_DENSITY`, e o renderer consulta
`SDL_GetCurrentRenderOutputSize` (pixels reais) em vez de `SDL_GetWindowSize`
(pontos lógicos) — a diferença entre os dois é o que faz a UI sair com metade do
tamanho num device retina.

## O que falta resolver

### Perda de contexto de GPU
Ao receber `SDL_EVENT_DID_ENTER_BACKGROUND`, o contexto gráfico pode ser destruído
e **todas as texturas ficam inválidas**. Hoje o atlas é criado uma vez em
`main()` e nunca recriado.

O que precisa: um `AssetManager` que saiba recriar suas texturas, e o tratamento de
`SDL_EVENT_WILL_ENTER_FOREGROUND` chamando esse reload. Como o atlas é gerado em
código (`Tileset::build_procedural`), isso hoje é fácil — só não está feito.

### UI escalável
O HUD é o título da janela. Num celular não existe título de janela. Precisa de UI
de verdade com alvos de toque dimensionados em unidades físicas, não em pixels.

### Safe areas
Notch e barra de gestos. `SDL_GetWindowSafeArea` existe no SDL3 e não é usado.

## Android: como buildar

Não escrevi um projeto Gradle à mão de propósito. O SDL3 **já traz um template
oficial** que é mantido junto com a lib; um Gradle escrito à mão aqui apodreceria
na primeira atualização do SDL.

O template está no checkout já travado do SDL:

```
.deps/sdl3-src/android-project/
```

Passos:

1. Instale Android Studio (traz SDK, NDK e o CMake do NDK).

2. Popule as dependências, se ainda não:
   ```bash
   cmake --preset debug   # baixa .deps/sdl3-src
   ```

3. Copie o template:
   ```bash
   cp -r .deps/sdl3-src/android-project platform/android/app-template
   ```

4. No `app/build.gradle` do template, aponte o `externalNativeBuild` para o
   `CMakeLists.txt` **da raiz deste repositório** e passe:
   ```
   arguments "-DGAME_BUILD_SERVER=OFF", "-DGAME_BUILD_TESTS=OFF", "-DANDROID_STL=c++_shared"
   ```
   `GAME_BUILD_SERVER=OFF` importa: não faz sentido compilar o servidor headless
   para dentro do APK.

5. A `SDLActivity` carrega a lib nativa por nome. `src/client/CMakeLists.txt` já
   detecta `ANDROID` e gera uma **shared library** chamada `main` em vez de um
   executável, que é o nome que o template espera.

6. Copie `assets/` para `app/src/main/assets/` do projeto Android. É de lá que o
   `SDL_IOFromFile` lê.

Detalhe importante: o `FETCHCONTENT_BASE_DIR` aponta para `.deps/` na raiz do
repositório, então o Gradle vai reaproveitar o mesmo download do build de desktop.

## iOS: como buildar

```bash
cmake -S . -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DGAME_BUILD_SERVER=OFF \
  -DGAME_BUILD_TESTS=OFF
open build/ios/game.xcodeproj
```

No Xcode, defina o time de assinatura e o bundle identifier no target
`game_client`. O SDL usa Metal no iOS, que é o motivo de `SDL_Render` já servir
aqui sem mudança nenhuma.

Assets precisam entrar como bundle resource do target, senão `SDL_LoadFile` não os
encontra.

## Recomendação de ordem

Coloque um build de Android rodando na CI **cedo**, mesmo que só compile e não
rode. Um build mobile que fica três meses sem ser exercitado não volta em uma
tarde: quebra em toolchain, em versão de NDK e em API level ao mesmo tempo, e você
não sabe qual mudança causou o quê.

O oposto também vale: não gaste tempo em polimento mobile antes de o jogo existir
no desktop. As decisões que importam já estão travadas nas seções acima.
