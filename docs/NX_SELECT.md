# NX — SelectSong (CSelect)

Análise do `piu` da NX (`F:\18_nx\game\piu`). Endereços são da NX.
Níveis de confiança: **Confirmado** (lido no assembly), **Provável**, **Hipótese**.

## Classe

- RTTI `7CSelect`, vtable `0x081105a8` — Confirmado.
  | slot | NX | Zero (comparação) | papel |
  | --- | --- | --- | --- |
  | 0/1 | 0x8079620 / 0x8079690 | 0x806a710 / 0x806a750 | destrutores |
  | 2 | 0x8079710 | 0x806a7a0 | Begin |
  | 3 | 0x807ab30 | 0x806b520 | ? |
  | 4 | 0x807aee0 | 0x806b530 | quadro |
  | 5 | 0x807bd40 | 0x806e240 | End |
- Ao contrário da Zero (quadro de 0x2d10 bytes), na NX o quadro é curto e chama auxiliares
  em `0x807bd50..0x807ecb0`.

## Recursos (Begin 0x8079710) — Confirmado

| campo | arquivo | uso |
| --- | --- | --- |
| `+0x32c` | `BGA/COMMON.DAT` (via 0x8062400; arg 2 se modo == 3, senão 1) | canal, setas, molduras de modo, nível |
| `+0x330` | `BGA/LEVEL.DAT` | estrelas/“hell”, números de nível |
| `+0x334` | `BGA/ARCADE_SPECIAL.DAT` | **carrossel** (`arcade_special.bga`, BGA3), cenas, corações |
| `+0x338` | `BGA/COMMAND.DAT` | ícones de modificador (`%dp-%dcommand`) |
| — | `BGA/BG.MOV` / `BGA/SP.MOV` | fundo |
| — | `/SCRIPT/UI/SFX_SELECT.LUA` | sons `EFF_*` |

Nomes de cena resolvidos no Begin (0x8058c60 = nome → id por hash):
`channel start/hold/end`, `channel text start/hold/end`, `arro start/end`,
`arroUL/UR/HL/HR click (hold)`, `center step`, `screen2 start/hold/end/click/L move/R move`,
`single|1p|2p mode text start/hold/end`, `single|1p|2p level position`,
`single|1p|2p star-hell / hell-star`, `single|1p|2p hell effect start/hold`,
`heart start/hold/end`, `heart%d`, `bonus`, `%dp-%dcommand`, `time position`.

API de cena da NX (Confirmado pelo assembly):
- `0x8058c60(bga, nome)` → id (índice na tabela `+0x1b0`, entradas de 12 bytes; -1 se não houver)
- `0x8058330(bga, id, desenha)` toca/desenha a cena; `0x8058a80(bga, id)` → terminou?
- `0x8058070(bga, camada)` lê e `0x80580c0(bga, camada, valor)` troca a fonte (sprite/textura) de uma camada.

## Lista de músicas — Confirmado

- **Uma lista circular só**, não uma lista por canal como na Zero. `[this+8]` = vetor de registros
  0x48 (cópias da tabela `0x0813c200`), `+0x20` contagem, `+0x14` cursor, `+0x18` id atual,
  `+0x80` ponteiro para o registro atual.
- `0x807e380` próxima (no fim volta a 0) / `0x807e3c0` anterior (em 0 vai para o fim).
- O canal é só um rótulo da música atual: `0x807d5a0` compara o canal anterior (`+0xc`) com o
  `+0x1C` do registro; se mudou, troca o texto do canal (camadas 1/4/5/0x63 do COMMON com
  `+0x20c/+0x210[canal]`, 0x31 com `+0x1dc[canal]`) e toca **EFF_CHANNEL**.

## Carrossel (0x807d820(this, direção)) — Confirmado (estrutura) / Provável (posições)

- Não reinicia animação de lista: a cada passo recalcula os vizinhos `(cursor ± k) mod contagem`
  e só **troca a fonte das camadas** do `arcade_special.bga` (`+0x334`):
  - textura do título da música = registro `+0x38` (`0x807e3f0`) → camadas `0x24`, `0x14`, …
  - ícone de canal `+0x1ac[canal]` → camadas `0x23`, `0x13`, …
  - cadeado `+0x324` (`lock.spr`) se o byte `+0x36` (disponível) == 0 → camadas `0x26`, `0x16`, …
- Painéis do BGA: `screen11..14.spr`, `position.spr`, `part1.spr` (título), `lock.spr`.
- direção: 1 / 2 (Hipótese: 1 = direita, 2 = esquerda; vem de `+0xa4`, posto em 1 no movimento p/ direita).

## Entrada por jogador (0x807cdb0(this, jogador)) — Confirmado

`0x8065920(pad, botão)`: 1 = apertou, 3 = segurando; `0x80658a0` = tempo segurando (ms).
Pads em `0x9e420a0 + jogador*0xccc`. Botões: **7 DL, 8 UL, 9 C, 0xA UR, 0xB DR**
(Provável pelo que cada ramo faz).

| botão | ação |
| --- | --- |
| DR (0xB) | 0x807cf80: próxima música, cena `+0xe0` (COMMON) e `+0xb8` (ARCADE_SPECIAL), carrossel, BPM (0x807c760). Segurando > 0x289 ms → **EFF_MOVE_ACC** com repetição de 0x1f4 ms; senão **EFF_MOVE** |
| DL (0x7) | 0x807d140: igual, música anterior |
| UR (0xA) | **próxima dificuldade disponível** (0x807e7a0) + **EFF_MODE** |
| UL (0x8) | **dificuldade anterior disponível** (0x807e710) + **EFF_MODE** |
| C (0x9) | 0x807d230: **EFF_SELECT** / confirma |
| — | 0x807d340: **EFF_WRONG** |

### Troca de dificuldade (o “só muda os níveis”) — Confirmado

- `0x807e7a0` / `0x807e710`: a partir da dificuldade atual `+0x8c[j]`, testa
  `(atual ± i) mod n`, i = 1..n-1, e pega a primeira com `nível[d] >= 0` (+0x20) **e**
  `aberta[d] != 0` (+0x3C). Se nenhuma servir, fica como está.
  - modo `0x81f8904 != 3`: n = 5, uma dificuldade compartilhada (`+0x8c`).
  - modo `== 3` (2 jogadores): n = 3, cada jogador com a sua (`+0x8c[j]`), só d <= 2.
- `0x807cce0(this, j, d, 0)`: troca a camada do nível no COMMON (0x38; 0x3d/0x3e por jogador no
  modo 3) para `+0x2e8[d]` e toca a cena `+0x104` (ou `+0x118[j]`); depois `0x807c970`
  atualiza os números/estrelas no LEVEL.DAT.
- **O carrossel não é tocado** na troca de dificuldade: só cena `+0xd4` do COMMON + nível.

### Nível (0x807c970) — Provável

- Guarda nível novo/antigo em `+0x36c` / `+0x374`; cruzar o limite **> 0x17 (23)** ou **> 0xe (14)**
  dispara a transição estrela ↔ “hell” (`star-hell` / `hell-star`) e `hell effect`.

## Início do jogo (quadro 0x807aee0) — Confirmado

- **EFF_START**, monta `RUN %X %s %s` com `-n/-h/-c/-d/-m` por dificuldade
  (tabela de saltos `0x8110454`).
- `EFF_HIDDEN_SELECTED` também aparece no quadro (código escondido).

## Em aberto

- Posições exatas das camadas do carrossel por slot (ler `arcade_special.bga`).
- `heart%d` / `bonus` (corações = créditos/vidas da NX? — Hipótese).
- Slot 3 da vtable (0x807ab30) e o contador de tempo (`time position`).

## Implementação (src/nx_select.c)

- Begin/quadro/entrada reconstruídos a partir de 0x8079710 / 0x807aee0 / 0x807cdb0.
- Carrossel 0x807d820 com os slots exatos (parado 15/7/11; DR = "screen2 L move"
  56/40/52/44/48; DL = "screen2 R move" 36/20/28/24/32), um objeto position.spr por slot.
- Prévia: vídeo na textura do card central (movie.spr, 0x807bb00) — o player de vídeo
  ganhou uma segunda instância (Movie_Select) para o BG.MOV continuar tocando.
- Nível 0x807c820/0x807c970, dificuldade 0x807e7a0/0x807e710/0x807e5f0, tempo 0x807e8e0.
- Correções de infraestrutura que a NX exigiu: nomes de entrada do RESPAC2 com mais de
  15 caracteres (arcade_special.bga) e MAX_BGA_SCENES 64 (COMMON tem 46 cenas).
- Faltam: texto artista/título/BPM em FreeType (NXTW.TTF), códigos/COMMAND.DAT, modo
  especial (corações, FULL SONG/REMIX/ANOTHER).
- Teste sem pad: PUMPY_AUTOSTATE=SELECT e PUMPY_SHOT=n1,n2 (capturas em pumpy_shot_n.bmp).

## Station (src/nx_station.c) — CStation 0x808a7f0

- Usa BGA/COMMON.DAT (cards "screen *", canal, nome, tempo de 25 s) e BGA/ARRO.DAT
  (setas; slots 0/2/4/6 recebem as variantes "Prev./Next" dos slots 18..21).
- 4 estações (+0x12c): 0 TRAINING, 1 ARCADE, 2 WORLD TOUR, 3 SPECIAL ZONE; modo de jogo
  [0x81f8998] pela tabela 0x8114978 = {3, 0, 2, 1}. UR/DR avançam ("screen L move"),
  UL/DL voltam ("screen R move"); rotação dos cards em 0x808bf90.
- Slot 1 do COMMON (painel de cima) recebe `+0x58/+0x60/+0x68/+0x70` = camadas 9, 1, 8, 7
  (`channel4/1/3/2.spr`, 0x808ac60..0x808ad1d). Antes lia 52/55 (`text_*`) por um `ebx` mal rastreado:
  o painel ficava transparente e o texto do canal aparecia no topo. Corrigido em 01/10/2026.
- TRAINING (CSelectEz) e WORLD TOUR (CSelectWorld) ainda vão para a Select arcade.
- SPECIAL ZONE (modo 1) = mesma CSelect: lista só canais 4..6 com custo <= corações
  (0x8062566; custo FULL SONG 4, REMIX 3, ANOTHER 2), fundo BGA/SP.MOV, corações
  [0x81f8a14] (7 no reset 0x8062930, gastos em 0x807b4fb), slots ARCADE 0x4b..0x51
  (fontes 2/0x62/0x41) + cena heart6 (0x807e980), texto do tipo 0x4a (0x5a..0x5c) e,
  com o extra [0x81f8a18], 0x52 (0x5e..0x60) e cena "bonus". Fim (0x8073a88): >1
  coração segue; senão todas as razões >= 0.95 dão +1 coração e o extra.
  Não feito: desbloqueio do Begin especial (0x807aa60).
