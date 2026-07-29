# Dependências

## O princípio

Nada é instalado no sistema. Todas as bibliotecas são buscadas pelo build e
travadas em **commit SHA exato**, não em tag.

Isso não é preciosismo: tags podem ser movidas por quem mantém o projeto upstream,
e uma tag movida transforma "funciona na minha máquina" num bug que ninguém
reproduz. Um SHA não muda. Se o build funciona hoje aqui, funciona amanhã na
máquina de outra pessoa.

As versões vivem em um único arquivo: [`cmake/Dependencies.cmake`](../cmake/Dependencies.cmake).

| Biblioteca | Versão | Para quê |
|---|---|---|
| SDL3 | release-3.4.12 | janela, input, áudio, ciclo de vida mobile. A única lib que cobre Win/Linux/Mac/Android/iOS com o mesmo código. |
| EnTT | v3.16.0 | ECS. Header-only, sem dependências. |
| glm | 1.0.3 | matemática vetorial. Header-only. |
| ENet | v1.3.18 | UDP confiável. Fica atrás de `net::ITransport`. |
| doctest | v2.5.3 | testes. |

O SDL é compilado **estático** de propósito: distribuir o cliente passa a ser um
executável só, sem ticket de suporte por `SDL3.dll` faltando. No Linux ele ainda
faz `dlopen()` de Wayland/X11/ALSA em tempo de execução, então isso não amarra o
servidor gráfico do usuário.

## Cache de dependências

O download vai para `.deps/` **na raiz do repositório**, não dentro de `build/`.
Duas consequências práticas:

- Vários presets (`debug`, `release`, `asan`) compartilham um download só.
- A pasta pode ser copiada para uma máquina sem internet.

`.deps/` está no `.gitignore` — ela é 100% reconstruível a partir do
`Dependencies.cmake`.

## Build offline

Numa máquina com internet:

```bash
cmake --preset debug        # popula .deps/
tar czf deps-cache.tar.gz .deps
```

Na máquina sem internet:

```bash
tar xzf deps-cache.tar.gz   # na raiz do repo
cmake --preset debug        # FetchContent vê que já está lá e não baixa nada
```

## Usar o SDL3 do sistema

Se você já tem SDL3 instalado e quer economizar o tempo de compilação:

```bash
cmake --preset debug -DGAME_USE_SYSTEM_SDL=ON
```

**Não use isso em CI nem para gerar build de release.** O ponto de travar a versão
é garantir que todos compilam o mesmo código; o SDL do sistema muda quando o
usuário atualiza a distro. Esse flag existe só para iteração local rápida. O
resumo impresso no fim do `cmake` diz qual origem está em uso, justamente para
essa escolha nunca ser silenciosa.

## Atualizar uma dependência

1. Descubra o SHA da versão nova:

   ```bash
   git ls-remote --tags https://github.com/libsdl-org/SDL.git | grep release-3.4
   ```

2. Edite o SHA **e o comentário da versão junto**, na mesma linha, em
   `cmake/Dependencies.cmake`.

3. Valide:

   ```bash
   ./scripts/verify-deps.sh          # confere que cada SHA existe e a que tag corresponde
   rm -rf .deps build
   cmake --preset ci && cmake --build --preset ci && ctest --preset ci
   ```

O `rm -rf .deps` importa: sem ele o FetchContent pode reaproveitar a árvore antiga
e você testa a versão errada.

## Por que CMake 4 precisa de um escape hatch

O CMake 4 removeu a compatibilidade com `cmake_minimum_required(VERSION < 3.5)`.
O ENet 1.3.18 ainda declara `2.8.12`, o que seria erro fatal de configure.
`Dependencies.cmake` define `CMAKE_POLICY_VERSION_MINIMUM 3.5`, que é a saída
oficial e afeta **só as dependências** — o nosso código continua exigindo 3.25.

## Por que as dependências são declaradas `SYSTEM`

Os nossos avisos de compilação são agressivos (`-Wconversion`,
`-Wsign-conversion`, `-Wold-style-cast`) e a CI usa `-Werror`. Headers de terceiros
não passam nesse crivo — e não é trabalho nosso consertá-los. Cada
`FetchContent_Declare` usa `SYSTEM`, então esses headers entram como `-isystem` e
os avisos deles são silenciados sem que a gente relaxe nada no `src/`.

## Como adicionar uma dependência nova

Faça no mesmo padrão, e resista à tentação de usar `find_package` sem fallback:

```cmake
set(GAME_DEP_FOO_REF "<sha completo>") # v1.2.3

FetchContent_Declare(foo
    GIT_REPOSITORY https://github.com/org/foo.git
    GIT_TAG        ${GAME_DEP_FOO_REF}
    ${GAME_DEP_SYSTEM})
FetchContent_MakeAvailable(foo)
```

Antes de adicionar, pergunte se o servidor vai precisar dela. Se não, ela só pode
ser linkada por `game_platform` ou `game_client` — ver
[architecture.md](architecture.md).
