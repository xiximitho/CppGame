# assets/

Dados de jogo somente-leitura, distribuídos junto com o executável.

**Neste momento esta pasta está quase vazia de propósito.** Toda a arte do
protótipo é gerada em código (`src/client/src/tileset.cpp`), o que significa que um
clone limpo do repositório compila e roda sem nenhum passo de download de asset e
sem depender de um decodificador de imagem. Ver [../docs/roadmap.md](../docs/roadmap.md)
item 6 para quando isso mudar.

## Regra importante

Nunca leia arquivos daqui com `<fstream>`. Use sempre `platform::vfs::read_asset()`.

No Android estes arquivos ficam **dentro do APK** e não são arquivos no
filesystem — `std::ifstream` não consegue abri-los, mas `SDL_IOFromFile` (que é o
que o VFS usa) consegue. Código que usa `<fstream>` funciona no desktop e falha no
celular, e falha meses depois de ter sido escrito.

Esta pasta é **read-only** em runtime. Save, configuração escrita e log vão para
`platform::user_root()`.

## Arquivos

| Arquivo | O quê |
|---|---|
| `client.cfg.example` | Modelo de configuração. Copie para `client.cfg` para usar. |
| `tilesets/` | Vazio. Onde os atlas de sprite vão entrar. |

`client.cfg` em si não é versionado: é configuração local de cada máquina.
