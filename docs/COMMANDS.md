# Códigos de comando e teclas

Códigos de passos aceitos na seleção de música (ARCADE STATION, SPECIAL ZONE) e no TRAINING
STATION do NXReconstructed, mais as teclas de operador e o que o GAME STYLE libera.
Cada código é digitado no tapete do próprio jogador.

- [Códigos de passos](#como-funcionam)
- [Extras do port (MODDED)](#extras-do-port-game-style--modded)
- [GAME STYLE: o que muda no MODDED](#game-style-o-que-muda-no-modded)
- [Teclas de operador](#teclas-de-operador)
- [Atalhos durante o jogo](#atalhos-durante-o-jogo)
- [Console de debug](#console-de-debug)

<img src="img/arrows/dl.png" width="26" alt="DL"> DL &nbsp; <img src="img/arrows/ul.png" width="26" alt="UL"> UL &nbsp; <img src="img/arrows/c.png" width="26" alt="C"> C &nbsp; <img src="img/arrows/ur.png" width="26" alt="UR"> UR &nbsp; <img src="img/arrows/dr.png" width="26" alt="DR"> DR

`DL` = baixo-esquerda · `UL` = cima-esquerda · `C` = centro · `UR` = cima-direita · `DR` = baixo-direita

## Como funcionam

- O jogo guarda os últimos 25 passos de cada jogador e compara o fim da sequência com a tabela.
- Um código vale para quem digitou, exceto os da seção [Para os dois jogadores](#para-os-dois-jogadores).
- O que está ligado aparece como ícone do `COMMAND.DAT` ao lado do jogador (até 5 posições).
- Dois códigos da mesma posição (por exemplo V e FD, ou 2X e +0.5X) se alternam na tela a cada 3 segundos.
- Os modificadores e a skin valem até o fim do crédito.
- O CENTER que fecha um código pertence ao código: ele não deixa a música em "CENTER STEP".
  A confirmação só vem com um CENTER dado depois.
- A seleção só aceita passos depois que a tela termina de entrar (carrossel, faixa do canal e o
  primeiro segundo). Um código começado antes disso precisa ser refeito.
- No ARCADE STATION e na SPECIAL ZONE com dois jogadores, cada um digita os próprios códigos; em
  Double e Nightmare os códigos de quem está jogando valem para o campo inteiro.
- **GAME STYLE** (Service Menu → GAME OPTION): em `VANILLA` só valem os códigos do arcade original;
  em `MODDED` valem também os da seção [Extras do port](#extras-do-port-game-style--modded).

## Velocidade

| Código | Sequência | Efeito |
|---|---|---|
| **Velocidade**<br><sub>UL UR UL UR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Cada vez que entra, avança: 2X → 3X → 4X → 8X → desligado. |
| **RV**<br><sub>UL UR UL UR UL UR UL UR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o RV. Ocupa o lugar da velocidade fixa. |
| **EW (Earthworm)**<br><sub>DR DL UR UL DR UR DL UL C</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Earthworm. Ocupa o lugar da velocidade fixa. |
| **AC (Accel)**<br><sub>DL DL DR DR UL UL UR UR C</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga a aceleração. Desliga o DC. |
| **DC (Decel)**<br><sub>DR DR DL DL UR UR UL UL C</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga a desaceleração. Desliga o AC. |

## Modificadores

| Código | Sequência | Efeito |
|---|---|---|
| **V / NS**<br><sub>UL UR DL DR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/c.png" width="26" alt="C"> | Cada vez que entra, avança: V (Vanish) → NS (Non-Step) → desligado. |
| **FL (Flash)**<br><sub>DL UL DR DL UR DR UL UR C</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Flash. Desliga V e NS. |
| **FD (Freedom)**<br><sub>UL DL UR DR DR UL UR DL C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Freedom. |
| **M (Mirror)**<br><sub>DR DL UR UL DR DL UR UL C</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Mirror. Desliga o RS. |
| **RS (Random Step)**<br><sub>UL UR UL UR DL DR DL DR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Random Step. Desliga o M. |
| **RG (Grade Reverse)**<br><sub>UL UR C DL DR DR DL C UR UL C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Grade Reverse. |

## Para os dois jogadores

Estes ligam e desligam nos dois lados ao mesmo tempo, não importa quem digitou.

| Código | Sequência | Efeito |
|---|---|---|
| **UA (Under Attack)**<br><sub>DL C DL C DL C UR C</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o Under Attack. |
| **X (X-Mode)**<br><sub>DL UR DL UR DR UL DR UL C</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga o X-Mode. |
| **NX (NX Mode)**<br><sub>DL UL C DR UR DL UL C DR UR</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> | Liga/desliga o NX Mode. |

## Skins de seta

| Código | Sequência | Efeito |
|---|---|---|
| **HATO**<br><sub>DR DR DR DL DR UL UR DL UR</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ur.png" width="26" alt="UR"> | Skin `SKIN01`. |
| **OR**<br><sub>DR DR DR DL DR UL UR DL C</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> | Skin `SKIN02`. |
| **CARD**<br><sub>DR DR DR DL DR UL UR DL UL</sub> | <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/ul.png" width="26" alt="UL"> | Skin `SKIN07`. |
| **CANO**<br><sub>UL UR DL C DL DR DR UR UR</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ur.png" width="26" alt="UR"> | Skin `SKIN06`. |

## Limpar

| Código | Sequência | Efeito |
|---|---|---|
| **Limpar**<br><sub>DL DR DL DR DL DR</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> | Zera velocidade e modificadores do jogador, volta a skin padrão (`SKIN08`) e desliga UA, X e NX dos dois jogadores. |

## Extras do port (GAME STYLE = MODDED)

Não existem no arcade original. As sequências das skins vêm da NX2.

| Código | Sequência | Efeito |
|---|---|---|
| **REbirth**<br><sub>UL UL UL DL DL DL DR UL DL</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/dl.png" width="26" alt="DL"> | Skin `SKIN00`. Sem ícone. |
| **EZ**<br><sub>UL UR DL C DL DR DR UR DL</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> | Skin `SKIN03`. Sem ícone. |
| **SLIME**<br><sub>UL UR DL C DL DR DR UR UL</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> | Skin `SKIN04`. |
| **MELODY**<br><sub>UL UR DL C DL DR DR UR DR</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/c.png" width="26" alt="C"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/dr.png" width="26" alt="DR"> | Skin `SKIN05`. |
| **5X e 6X**<br><sub>UL UR UL UR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | O mesmo código da velocidade passa a avançar 2X → 3X → 4X → 5X → 6X → 8X → desligado. |
| **+0.5X**<br><sub>UL UL UR UR C</sub> | <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ul.png" width="26" alt="UL"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/ur.png" width="26" alt="UR"> <img src="img/arrows/c.png" width="26" alt="C"> | Liga/desliga meio ponto somado à velocidade fixa: 1.5X, 2.5X, 3.5X... Não vale com RV nem EW. |
| **BGA Off**<br><sub>DL DL DL DL DR DR DR DR C</sub> | <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dl.png" width="26" alt="DL"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/dr.png" width="26" alt="DR"> <img src="img/arrows/c.png" width="26" alt="C"> | Troca o BGA da música por um fundo de estrelas (TestBGA). |

Observações:

- A AC termina com os mesmos cinco passos do +0.5X. Quem digita a AC inteira continua pegando a
  AC; só a sequência curta, sozinha, liga o +0.5X.
- O código de limpar (DL DR DL DR DL DR) também desliga o +0.5X.

## GAME STYLE: o que muda no MODDED

O GAME STYLE fica no Service Menu, em GAME OPTION. O padrão é `VANILLA`, que se comporta como o
arcade. Em `MODDED`:

| O que | Como fica |
|---|---|
| Códigos extras | Valem os da seção acima (skins REbirth, EZ, SLIME e MELODY, 5X, 6X, +0.5X, BGA Off). |
| Músicas extras | Aparecem **Fighting Spirit** (H.O.T) e **Valenti** (BoA) no canal K-POP, e **Fighting Spirit Another** na SPECIAL ZONE. Os arquivos já estão no disco do NX; só não constam na lista do arcade. |
| Atração em FREE PLAY / EVENT | A atração roda (logo, NEXCADE, abertura, título, demo, ranking, WPF2007) em vez de ficar presa no título. O CENTER leva ao título, e o título volta à atração sozinho. |
| Dance Grade | São precisos três CENTER para pular a tela (no VANILLA, um). |
| Opções extras no GAME OPTION | Aparecem três linhas abaixo do GAME STYLE (tabela seguinte). |

Opções que só existem, e só valem, com o estilo em `MODDED`:

| Opção | Padrão | O que faz |
|---|---|---|
| UNLOCK SPECIAL ZONE | OFF | Libera todas as músicas e dificuldades da SPECIAL ZONE. |
| AUTOPLAY KEY (F8) | OFF | Permite ligar e desligar o autoplay com o F8 durante a música. |
| DG TEST KEY (F12) | OFF | Permite encerrar a música com o F12 e ir ao Dance Grade com valores fixos. |

As três são gravadas no `PUMPY.INI` ao sair pelo EXIT. O DEFAULT SETTING desliga todas e volta o
estilo para `VANILLA`.

## Teclas de operador

| Tecla | Fora do Service Menu | Dentro do Service Menu |
|---|---|---|
| F1 | Abre o Service Menu. | TEST: move o cursor. |
| F2 | SERVICE: crédito de cortesia. | SERVICE: altera ou confirma. |
| F3 | COIN CLEAR: zera moedas e créditos. | CLEAR. |
| F4 | COIN1: uma moeda. | COIN1. |
| F5 | COIN2: vale os créditos configurados para a segunda entrada. | |
| F11 | Liga/desliga as informações de debug. | |
| ESC | Na atração, passa para a próxima tela. Durante um crédito, volta ao logo e zera o crédito. | Sai, como a opção EXIT. |
| Alt+Enter | Alterna janela e tela cheia exclusiva. Com o Alt segurado, o Enter não chega ao jogo. | |
| Alt+F4 | Fecha o jogo. | |
| Crase (tecla à esquerda do 1) | Abre o console de debug. | |

Sobre o COIN CLEAR: na tela de título, o "CENTER STEP" volta para "INSERT COIN" na hora. Em FREE
PLAY ou EVENT ele não muda nada, porque o crédito não vem de moeda. Quem já entrou no jogo
continua jogando.

A tela cheia do Alt+Enter usa a RESOLUTION do GRAPHICS SETTINGS e não é gravada: ao reabrir vale
o que está no `PUMPY.INI`.

## Atalhos durante o jogo

| Onde | Tecla ou passo | O que faz |
|---|---|---|
| Dance Grade | CENTER | Pula para a próxima tela: um CENTER no `VANILLA`, três no `MODDED`. Só conta depois de meio segundo de tela. |
| Música | F8 | Liga/desliga o autoplay. Precisa de `MODDED` e AUTOPLAY KEY ligada. O autoplay fica ligado nas músicas seguintes até ser desligado. |
| Música | F12 | Encerra a música e mostra o Dance Grade com valores fixos (score 123456, nota A). Precisa de `MODDED` e DG TEST KEY ligada. |

Com o autoplay ligado aparece o aviso "AUTOPLAY" na tela, e o `pumpy.log` registra quando e como
ele foi ligado (procure por `AUTOPLAY:`).

## Console de debug

Comandos do console do projeto. Não dependem do GAME STYLE.

| Comando | O que faz |
|---|---|
| `/skin N` | Troca a skin dos dois jogadores para `SKIN0N.DAT` (0 a 8), a partir da próxima música. |
| `/autoplay` | Só avisa que o autoplay é pelo F8 durante a música. |
| `/drawfps` | Liga/desliga o contador de FPS. |
| `/play <step> <modo>` | Abre um step para teste (`-n -h -d -c -hd -dv`). |
| `/testmode [faixa] [modo]` | Testa todas as músicas em sequência. |
| `/highscore` | Gerencia o ranking. |
| `/history` | Mostra o histórico de comandos. |
| `/help` | Lista os comandos. |

## Fontes

- Tabela de códigos e efeitos: `k_nxCodes` e `applyCode` em `src/nx_select.c`
  (verificador `0x804d240`, tabela `0x8143ca0` e efeitos `0x807c050` do `piu`).
- Nomes dos modificadores: siglas dos ícones do `COMMAND.DAT` e nomes das variáveis do projeto
  (`cmdToGame`).
- Sprites das setas: quadro 0 de `skin1..5.spr` da skin padrão (`BGA/SKIN08.DAT`).
- Alternância dos ícones a cada 3 s: desenho das posições de ícone no `piu` (`0x808e130`).
- Teclas de operador e ESC: `Game_Update` em `src/main.c`; Alt+Enter em `src/input.c`.
- Opções do GAME STYLE: `svcRenderGameOption` em `src/service_menu.c`.
- F12: "clear cheat" do NX2 (`playengine.cpp`, `m_bClear`).
