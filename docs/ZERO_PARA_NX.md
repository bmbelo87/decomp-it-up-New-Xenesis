# Zero → NX (New Xenesis): levantamento inicial

Comparação entre `piu` da Zero (`E:\Pumps\ZeroPC\Zero PC\piu`) e da NX (`F:\18_nx\game\piu`,
`version` = 108). Fonte: `readelf`, `strings` e verificação de todos os assets.

## Proteção / dongle — Confirmado

- Continua MicroDog 3.4. A chave da NX vem do pumptools: `src/main/hook/nx/nxhook.dog.key`
  → `tools/data/nx_dog.key` (mesmo layout: entradas de 0x4C a partir de 0xE0; 778 consultas de 16 bytes).
- Os algoritmos ENC2 (`.AUD`/`.PNZ`), RESPAC2 (`.DAT`, inclusive `STEP.DAT`) e MOV3 (`.MOV`) **não mudaram**:
  `tools/zero_decrypt.py` (agora com `nx_dog.key` por padrão, `--dog` para trocar) decifrou os
  694 arquivos da NX sem falha (Adler-32 OK). Nenhum arquivo ENC1 na NX.
- `src/zero_dog.c` foi regenerado com a chave da NX (`tools/gen_zero_dog.py`).
- Aparecem strings `BF_encrypt`/`BF_cbc_encrypt` (Blowfish) — uso ainda desconhecido (hipótese: save/USB).

## Executável

| Seção | Zero | NX |
| --- | --- | --- |
| `.text` | 0x0804ca50, 0xb03e0 | 0x0804cb00, 0xad540 |
| `.rodata` | 0x080fce60, 0x16f84 | 0x080fa060, 0x3bb90 |
| `.data` | 0x08117000, 0xc540 | 0x08137000, 0xe700 |
| `.bss` | 0x081345a0, 0x1a80a50 | 0x0814b900, 0x286d390 |

Mesmas bibliotecas dinâmicas. **Todos os endereços citados no código (0x80xxxxx) são da Zero e
precisam ser relocalizados na NX.** FreeType parece linkado estaticamente (strings de CID/Type1/TrueType)
e há fontes TTF (`MICROGBE.TTF`, `NXTW.TTF`).

## Nomes de recursos — Confirmado (strings)

- Músicas/BGA agora com 3 dígitos hex: `BGA/%03X.DAT`, `BGA/%03X.MOV`, `BGA/PREVIEW/%03X.MOV`,
  `AUDIO/INTRO/%03X.AUD`, `TITLE/%03X.PNZ` (Zero: `%X`, `D%X.AUD`, `BGA/PREVIEW/%X.MOV`).
- Settings: `/SETTINGS/PIUNX.INI` (Zero: `PIUZERO.INI`).
- Scripts: `SCRIPT/UI/*.LUA` sem subpastas `COMMON/EZ/ZERO`; novos `WORLDGRADE.LUA`, `SFX_GLOBAL.LUA`,
  `SETUP_T.LUA`, `KEY.LUA`; removidos `BATTLEMODE/MISSIONPOINT/NORMALMODE.LUA`, `SETUP_CN/SP.LUA`.
- Novos DAT/MOV: `ARCADE_SPECIAL`, `ARRO`, `COMMAND`, `COMMON`, `FONT`, `KFONT`, `WFONT`, `WF`, `LEVEL`,
  `NCHANGE`, `SPORTS`, `TEST`, `TRAINING(.MOV)`, `UNLOCK`, `WORLDGRADE`, `WORLDTOUR01/02`, `BG.MOV`,
  `CONQUEST.MOV`, `WT.MOV`, `SKIN08..SKIN12`, `AUDIO/STAGEBREAK.AUD`.
- Removidos: Station/EZ/Mission da Zero (`STATION.DAT`, `SELECT*.DAT`, `SKINEZ.DAT`, `EZ_*.MOV`,
  `A_INTRO_*.MOV`, `NST*.MOV`, `GAMEOVER.MOV`, `REWARD*.DAT`, `ICON*.DAT`, `MHIS.DAT`, `GRADEEZ.DAT`, ...).

## Modos / jogo — Provável (strings)

- World Tour (`WORLDTOUR*`, `WT.MOV`, `WORLDGRADE`), Training, Sports, Conquest.
- Itens no gameplay: Heart, Mine, Potion, Velocity, passos ocultos (textos das missões em EN/ES).
- I/O: USB e fallback MK5 (`CMainEngine::Init`).
- Classes com logs novos: `CBGA` (LoadV2/LoadV3), `CEffectManager`, `CMainEngine`, `CPlayEngine`,
  `CSetup`, `CStep`, `CTextureManager`. `CTutorial` saiu.

## Tabela de músicas — Confirmado

- `0x0813c200`, 192 registros de 0x48 bytes (contagem em `0x8061be0`); textos em **UTF-8**
  (Zero: CP949) e BPM como texto. Gerada por `tools/gen_nx_songs.py` → `src/nx_songs.c`.
- 7 canais pelo campo +0x1C (nomes por hipótese): NX 29, K-POP 45, POP 16, BANYA 38,
  FULL SONG 8, REMIX 19, ANOTHER 37.
- Mapa chart → recurso em `0x0813f860` (328 pares, `0x8061d10`): Another (`DB18` → `B18`),
  World Tour (`AA011` → `103`), Training (`DDD312A` → `312`).

## Steps — Confirmado (magic) / em aberto (layout)

- `STEP.DAT` (RESPAC2) traz 553 arquivos **`.SEE`** (`CStep::LoadStep` abre `%s.SEE`), magic `STEE`,
  versão 1, tabela de offsets em 0x100. **Não é STX**: precisa de loader novo.

## SelectSong

Ver `docs/NX_SELECT.md`.

## Próximos passos

1. Loader de `.SEE`.
2. Ajustar formatos de nome (`%03X`) no carregamento.
3. Nova tela de seleção (CSelect da NX).
4. Relocalizar as funções já reconstruídas (cifras, julgamento, skins, grade) no `piu` da NX.
