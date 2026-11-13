"""Textos do assistente Old Data (tools/old_data_wizard.py), por idioma.

Para acrescentar um idioma: copie o bloco de "en", troque o código e traduza os valores.
Chaves que faltarem num idioma caem no inglês. "{0}", "{1}" são preenchidos por T().
"""

LANGS = [("pt", "Português (Brasil)"), ("en", "English")]

_PT_INTRO = """\
O que é isto

O disco do Pump It Up NX traz o áudio, o BGA e o step de várias músicas que saíram da lista do
arcade, mas sem a capa, a prévia da seleção e o disco da seleção. O Pumpy.exe consegue usar
essas músicas (GAME OPTION > USE OLD DATA SONGS), buscando o que falta numa versão anterior do
jogo instalada ao lado do NX. Falta só uma coisa que o jogo não cria sozinho: a prévia
(BGA/PREVIEW/<ID>.MOV). É isso que este assistente faz.

O que você precisa ter

  1. A pasta do jogo NX, com o Pumpy.exe dentro dela.
  2. Pelo menos uma versão anterior do jogo, instalada em uma pasta irmã, seguindo a árvore da
     aba "2. Pastas" (o nome da pasta precisa começar com o número da versão: 17_zero, 16_exc2...).
  3. Os arquivos de chave nx_dog.key e zero_dog.key, numa pasta "data" ao lado deste programa
     (eles NÃO vêm junto: sem eles o assistente não consegue ler os arquivos cifrados do NX e do Zero).
  4. Nada mais: o ffmpeg já vai junto com este programa.

Passo a passo

  1. Aba "2. Pastas": escolha a pasta do jogo. O programa confere a árvore e mostra o que achou.
  2. Aba "3. Músicas": veja, música por música, o que existe e o que falta.
  3. Aba "4. Gerar prévias": clique em Começar e espere. Para as músicas cujo fundo é um .DAT
     (animação), o jogo é aberto sozinho por alguns segundos para gravar o fundo: é normal uma
     janela do jogo aparecer e sumir a cada música. Não mexa nela.
  4. Abra o jogo: Service Menu > GAME OPTION > GAME STYLE = MODDED e depois USE OLD DATA SONGS = ON.

Sem as versões anteriores as músicas continuam aparecendo e dá para jogar, só que sem capa, com
a prévia padrão e sem disco.
"""

_EN_INTRO = """\
What is this

The Pump It Up NX disc ships the audio, the BGA and the step file of many songs that were
removed from the arcade list, but without their cover, the selection preview and the selection
disc. Pumpy.exe can use those songs (GAME OPTION > USE OLD DATA SONGS), taking whatever is
missing from an older version of the game installed next to the NX one. Only one thing is not
created by the game itself: the preview (BGA/PREVIEW/<ID>.MOV). That is what this assistant does.

What you need

  1. The NX game folder, with Pumpy.exe inside it.
  2. At least one older version of the game, installed in a sibling folder, following the tree on
     the "2. Folders" tab (the folder name must start with the version number: 17_zero, 16_exc2...).
  3. The key files nx_dog.key and zero_dog.key, in a "data" folder next to this program
     (they are NOT included: without them the assistant cannot read the encrypted NX and Zero files).
  4. Nothing else: ffmpeg is already shipped with this program.

Step by step

  1. "2. Folders" tab: pick the game folder. The program checks the tree and shows what it found.
  2. "3. Songs" tab: see, song by song, what exists and what is missing.
  3. "4. Generate previews" tab: click Start and wait. For songs whose background is a .DAT
     (sprite animation) the game itself is opened for a few seconds to record the background: a
     game window showing up and disappearing for each song is normal. Do not touch it.
  4. Open the game: Service Menu > GAME OPTION > GAME STYLE = MODDED, then USE OLD DATA SONGS = ON.

Without the older versions the songs still appear and are playable, just without cover, with the
default preview and without the disc.
"""

_PT_TREE = """\
<raiz>\\                    qualquer pasta (ex.: E:\\ ou D:\\Jogos\\)
|
+-- 18_nx\\                 pasta da versão ATUAL: "NN_nome", NN = número da versão
|   +-- game\\              <== PASTA DO JOGO (o Pumpy.exe fica aqui)
|       +-- AUDIO\\         <ID>.AUD  e  INTRO\\<ID>.AUD
|       +-- BGA\\           <ID>.MOV ou <ID>.DAT, PREVIEW\\, 90.DAT
|       +-- TITLE\\         capas (<ID>.PNZ)
|       +-- STEP.DAT        steps de todas as músicas
|       +-- ...             SCRIPT\\, SETTINGS\\, WAVE\\
|
+-- 17_zero\\               versões ANTERIORES: NN MENOR que o da atual
|   +-- game\\              o nome da pasta interna precisa ser "game"
|       +-- AUDIO\\  BGA\\  TITLE\\
|
+-- 16_exc2\\game\\
+-- 15_exc\\game\\
+-- 14_px3\\game\\

Regras (as mesmas do jogo):
 - o jogo sobe DUAS pastas a partir da pasta do jogo para achar a <raiz>;
 - vale qualquer pasta "NN_nome" da <raiz> com NN menor que o da versão atual;
 - a versão mais próxima (maior NN) que tiver o arquivo é a usada.

O que é procurado em cada versão anterior:
 - capa      TITLE\\T<ID>E.PNZ, T<ID>H.PNZ ou T<ID>.PNZ
 - prévia    BGA\\PREVIEW\\<ID>.MOV (ou ENG\\ / HAN\\)
 - disco     <ID>.TGA dentro de BGA\\90.DAT, 90E.DAT ou 90H.DAT
 - BGA       BGA\\<ID>.MOV ou <ID>.DAT
 - áudio     AUDIO\\<ID>.AUD  (intro: AUDIO\\D<ID>.AUD)
"""

_EN_TREE = """\
<root>\\                    any folder (e.g. E:\\ or D:\\Games\\)
|
+-- 18_nx\\                 CURRENT version folder: "NN_name", NN = version number
|   +-- game\\              <== GAME FOLDER (Pumpy.exe goes here)
|       +-- AUDIO\\         <ID>.AUD  and  INTRO\\<ID>.AUD
|       +-- BGA\\           <ID>.MOV or <ID>.DAT, PREVIEW\\, 90.DAT
|       +-- TITLE\\         covers (<ID>.PNZ)
|       +-- STEP.DAT        step files of every song
|       +-- ...             SCRIPT\\, SETTINGS\\, WAVE\\
|
+-- 17_zero\\               OLDER versions: NN LOWER than the current one
|   +-- game\\              the inner folder must be named "game"
|       +-- AUDIO\\  BGA\\  TITLE\\
|
+-- 16_exc2\\game\\
+-- 15_exc\\game\\
+-- 14_px3\\game\\

Rules (the same as the game's):
 - the game goes UP TWO folders from the game folder to find the <root>;
 - any "NN_name" folder in the <root> with NN lower than the current version counts;
 - the closest version (highest NN) that has the file is the one used.

What is looked up in each older version:
 - cover     TITLE\\T<ID>E.PNZ, T<ID>H.PNZ or T<ID>.PNZ
 - preview   BGA\\PREVIEW\\<ID>.MOV (or ENG\\ / HAN\\)
 - disc      <ID>.TGA inside BGA\\90.DAT, 90E.DAT or 90H.DAT
 - BGA       BGA\\<ID>.MOV or <ID>.DAT
 - audio     AUDIO\\<ID>.AUD  (intro: AUDIO\\D<ID>.AUD)
"""

S = {
    "en": {
        "title": "NXReconstructed - Old Data Assistant",
        "lang": "Language:",
        "tab1": "1. What to do", "tab2": "2. Folders", "tab3": "3. Songs", "tab4": "4. Generate previews",
        "intro": _EN_INTRO, "tree": _EN_TREE,
        "game_folder": "NX game folder:", "browse": "Browse...", "verify": "Check",
        "expected": "Expected tree", "found": "What was found",
        "pick_game": "Pick the game folder.", "browse_title": "NX game folder (where Pumpy.exe is)",
        "pick_game_tab2": "Pick the game folder on tab 2.", "pick_game_first": "Pick the NX game folder on tab 2.",
        "show_all": "also show the ones that already have a preview",
        "sum_none": "Pick the game folder on tab 2.",
        "sum": "{0} songs can have the preview generated; {1} lack what is needed.",
        "sum_err": "Could not list the songs: {0}",
        "c_id": "ID", "c_audio": "Audio", "c_intro": "Intro", "c_bg": "Background", "c_prev": "Preview",
        "c_state": "Status", "c_src": "Audio comes from",
        "yes": "yes",
        "st_have": "already has preview", "st_noaudio": "no audio", "st_nointro": "no intro",
        "st_nobg": "no background", "st_gen": "can generate",
        "o_force": "redo the ones that already exist", "o_dry": "simulate only (writes nothing)",
        "o_ids": "Only these IDs (optional, e.g. 102,A07):",
        "start": "Start", "cancel": "Cancel", "canceling": "\nCanceling at the end of the current song...\n",
        "r_done": "Done.", "r_canceled": "Canceled.", "r_exit": "Finished (code {0}).", "r_err": "ERROR: {0}",
        "k_nodir": "The game folder does not exist: {0}",
        "k_sub_ok": "game\\{0} found",
        "k_sub_err": "game\\{0} NOT found (is this really the NX game folder?)",
        "k_step_ok": "STEP.DAT found", "k_step_warn": "STEP.DAT not found",
        "k_exe_ok": "Executable: {0}",
        "k_exe_warn": "Pumpy.exe is not in the game folder (only affects songs with a .DAT background)",
        "k_ver_bad": "The folder above the game folder is called '{0}': it must be 'NN_name' (e.g. 18_nx). "
                     "The game cannot find the older versions like this.",
        "k_ver_ok": "Current version: {0} ('{1}')",
        "k_noold": "No older version in '{0}' (expected something like {1}\\game)",
        "k_old_miss": "Older version '{0}': folders missing: {1}",
        "k_old_ok": "Older version '{0}' found",
        "k_old_nogame": "'{0}' looks like an older version, but has no 'game' folder inside",
        "k_ff_ok": "ffmpeg: {0}", "k_ff_err": "ffmpeg not found (it should be next to the program)",
        "k_key_ok": "Key file {0} found",
        "k_key_err": "Key file {0} NOT found: put it in a 'data' folder next to the program",
        "need_keys": "The key files are missing (see tab 2). Generating previews is blocked until they are in place.",
    },
    "pt": {
        "title": "NXReconstructed - Assistente Old Data",
        "lang": "Idioma:",
        "tab1": "1. O que fazer", "tab2": "2. Pastas", "tab3": "3. Músicas", "tab4": "4. Gerar prévias",
        "intro": _PT_INTRO, "tree": _PT_TREE,
        "game_folder": "Pasta do jogo NX:", "browse": "Procurar...", "verify": "Verificar",
        "expected": "Árvore esperada", "found": "O que foi encontrado",
        "pick_game": "Escolha a pasta do jogo.", "browse_title": "Pasta do jogo NX (onde fica o Pumpy.exe)",
        "pick_game_tab2": "Escolha a pasta do jogo na aba 2.", "pick_game_first": "Escolha a pasta do jogo NX na aba 2.",
        "show_all": "mostrar também as que já têm prévia",
        "sum_none": "Escolha a pasta do jogo na aba 2.",
        "sum": "{0} músicas podem ter a prévia gerada; {1} não têm o que é preciso.",
        "sum_err": "Não foi possível listar as músicas: {0}",
        "c_id": "ID", "c_audio": "Áudio", "c_intro": "Intro", "c_bg": "Fundo", "c_prev": "Prévia",
        "c_state": "Situação", "c_src": "Áudio vem de",
        "yes": "sim",
        "st_have": "já tem prévia", "st_noaudio": "sem áudio", "st_nointro": "sem intro",
        "st_nobg": "sem fundo", "st_gen": "pode gerar",
        "o_force": "refazer as que já existem", "o_dry": "só simular (não grava nada)",
        "o_ids": "Só estes IDs (opcional, ex.: 102,A07):",
        "start": "Começar", "cancel": "Cancelar", "canceling": "\nCancelando ao fim da música atual...\n",
        "r_done": "Concluído.", "r_canceled": "Cancelado.", "r_exit": "Terminou (código {0}).", "r_err": "ERRO: {0}",
        "k_nodir": "A pasta do jogo não existe: {0}",
        "k_sub_ok": "game\\{0} encontrada",
        "k_sub_err": "game\\{0} NÃO encontrada (esta é mesmo a pasta do jogo NX?)",
        "k_step_ok": "STEP.DAT encontrado", "k_step_warn": "STEP.DAT não encontrado",
        "k_exe_ok": "Executável: {0}",
        "k_exe_warn": "Pumpy.exe não está na pasta do jogo (só afeta as músicas com fundo .DAT)",
        "k_ver_bad": "A pasta acima da do jogo se chama '{0}': precisa ser 'NN_nome' (ex.: 18_nx). "
                     "O jogo não acha as versões anteriores assim.",
        "k_ver_ok": "Versão atual: {0} ('{1}')",
        "k_noold": "Nenhuma versão anterior em '{0}' (esperado algo como {1}\\game)",
        "k_old_miss": "Versão anterior '{0}': faltam as pastas {1}",
        "k_old_ok": "Versão anterior '{0}' encontrada",
        "k_old_nogame": "'{0}' parece uma versão anterior, mas não tem a pasta 'game' dentro",
        "k_ff_ok": "ffmpeg: {0}", "k_ff_err": "ffmpeg não encontrado (deveria estar ao lado do programa)",
        "k_key_ok": "Arquivo de chave {0} encontrado",
        "k_key_err": "Arquivo de chave {0} NÃO encontrado: coloque-o numa pasta 'data' ao lado do programa",
        "need_keys": "Faltam os arquivos de chave (veja a aba 2). Gerar prévias fica bloqueado até eles estarem no lugar.",
    },
}

_lang = "en"


def set_lang(code):
    global _lang
    if code in S:
        _lang = code


def get_lang():
    return _lang


def T(key, *args):
    s = S.get(_lang, {}).get(key) or S["en"].get(key, key)
    return s.format(*args) if args else s
