#!/usr/bin/env python3
"""Import Old Data — cria as prévias que faltam em BGA/PREVIEW do Pump It Up NX.

Para cada música da tabela (src/nx_songs.c) sem BGA/PREVIEW/<ID>.MOV:
  1. acha o áudio completo (AUDIO/<ID>.AUD) e a intro da seleção
     (NX: AUDIO/INTRO/<ID>.AUD; até o Zero: AUDIO/D<ID>.AUD);
  2. decodifica os dois e procura, por correlação cruzada, em que instante da música a
     intro se encaixa;
  3. recorta esse trecho do vídeo de fundo (BGA/<ID>.MOV), reduz para 256x192 a 30 qps
     em MPEG-2 (o formato das prévias do NX) e grava BGA/PREVIEW/<ID>.MOV no formato
     MOV3 do NX (cabeçalho, nome interno cifrado e embaralhamento do vídeo).
Músicas cujo fundo é um .DAT (animação de sprites) não têm vídeo para recortar: o próprio
jogo é aberto por alguns segundos em modo de captura (PUMPY_BGACAP, src/main.c), desenha
o trecho do BGA e os quadros viram a prévia. Uma janela do jogo aparece a cada música.

Os arquivos são procurados na pasta do jogo e nas versões anteriores ao lado dela
(<raiz>/NN_nome/game com NN menor que o da versão atual, da mais próxima para a mais
antiga), como o USE OLD DATA SONGS do jogo (src/old_data.c).

Requer ffmpeg no PATH e numpy. Uso:
  import_old_data.py [--game PASTA] [--exe JOGO] [--ids 102,A07] [--force] [--dry-run] [--report ARQ]
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from respack_extract import KeyGen  # noqa: E402  (cifra do Exceed)

import numpy as np  # noqa: E402

BITREV = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))
RATE = 8000          # Hz da comparação de áudio
PREVIEW_W, PREVIEW_H, PREVIEW_FPS = 256, 192, 30
# BGA guardado com outro id no NX (src/loading.c): Novasonic Remix usa o 326.DAT
BGA_ALIAS = {"DB52": "326"}
ENCODE = ["-an", "-vf", None, "-r", str(PREVIEW_FPS), "-c:v", "mpeg2video", "-b:v", "900k", "-maxrate", "1200k",
          "-bufsize", "1835k", "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-f", "mpeg2video"]


def encode_args(vf):
    return [vf if a is None else a for a in ENCODE]


def capture_dat(exe, game, dat, t0, dur, tmp):
    """Abre o jogo em modo de captura e devolve (arquivo de quadros, largura, altura)."""
    jobs, raw = os.path.join(tmp, "jobs.txt"), os.path.join(tmp, "cap.raw")
    open(jobs, "w", encoding="ascii").write("%s;%.3f;%.3f;%s\n" % (dat.replace("\\", "/"), t0, dur, raw))
    log = os.path.join(game, "pumpy.log")
    keep = open(log, "rb").read() if os.path.exists(log) else None
    try:
        subprocess.run([exe], cwd=game, env=dict(os.environ, PUMPY_BGACAP=jobs), timeout=180)
    finally:
        if keep is not None:
            open(log, "wb").write(keep)            # o log do jogador não é trocado pelo da captura
    if not os.path.exists(raw) or os.path.getsize(raw) < 16:
        raise ValueError("o jogo não gravou a captura (o executável tem o modo PUMPY_BGACAP?)")
    w, h = struct.unpack("<ii", open(raw, "rb").read(8))
    return raw, w, h


# ───────────────────────────── dongle / cifras ─────────────────────────────
def load_dog():
    """Respostas do MicroDog (NX e Zero) para consultas de 16 bytes."""
    tab = {}
    for key in ("nx_dog.key", "zero_dog.key"):
        p = os.path.join(HERE, "data", key)
        if not os.path.exists(p):
            continue
        raw = open(p, "rb").read()
        for o in range(0xE0, len(raw) - 0x4B, 0x4C):
            resp, _, n = struct.unpack_from("<III", raw, o)
            if n == 16:
                tab.setdefault(raw[o + 12:o + 28], (resp, key))
    return tab


DOG = load_dog()


def x2_key(data, resp):
    """piu 0x80a3dc0: out[i] = in[i] ^ k[i&3], k[j] = (k[j] + i) ^ 0x1C."""
    k = bytearray(struct.pack(">I", resp))
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ k[i & 3]
        k[i & 3] = ((k[i & 3] + i) ^ 0x1C) & 0xFF
    return bytes(out)


def dog_resp(q):
    r = DOG.get(bytes(q))
    return r[0] if r else 0      # fora da tabela: resposta 0 (Exceed 2)


def enc2_decode(buf):
    """.AUD / .PNZ (ENC2). Tenta o layout do Exceed 2 / Zero / NX e confere o Adler-32;
    depois o do Exceed (sem chave de 16 bytes). Igual a Resource_DecryptENC2."""
    if buf[:4] != b"ENC2":
        return None
    size, skip = struct.unpack_from("<II", buf, 0x84)
    for mode in (0, 1):
        off = 0x8C + (0x10 if mode == 0 else 0) + skip
        if off + 4 + 0x400 > len(buf):
            continue
        seed = struct.unpack_from("<I", buf, off)[0]
        T = buf[off + 4:off + 4 + 0x400]
        if mode == 0:
            K = x2_key(buf[0x8C:0x9C], dog_resp(buf[0x8C:0x9C]))
            table = bytes(T[j] ^ K[j & 15] for j in range(0x400))
        else:
            kg = KeyGen()
            table = b"".join(kg.derive(T[k:k + 16]) for k in range(0, 0x400, 16))
        src = buf[off + 4 + 0x400:][:size]
        rot = (table * ((len(src) >> 10) + 2))[seed & 0x3FF:][:len(src)]
        a = np.frombuffer(src.translate(BITREV), dtype=np.uint8)
        dec = (a ^ np.frombuffer(rot, dtype=np.uint8)).tobytes()
        if mode == 1 or (zlib.adler32(dec) & 0xFFFFFFFF) == seed:
            return dec
    return None


def enc1_decode(buf):
    """ENC1 (intros do NX, piu 0x80a42e0 / 0x80a4010): hdr[0x86]; tamanho = u32@0x7E ^ 0xCCBB;
    pula u32@0x82; semente = Adler-32; saída[i] = bitrev(src[i]) ^ T[(semente+i) & 0x3FF],
    T fixa de 1 KB (tools/data/zero_enc1_table.bin)."""
    p = os.path.join(HERE, "data", "zero_enc1_table.bin")
    if buf[:4] != b"ENC1" or not os.path.exists(p):
        return None
    T = open(p, "rb").read()
    size = struct.unpack_from("<I", buf, 0x7E)[0] ^ 0xCCBB
    off = 0x86 + struct.unpack_from("<I", buf, 0x82)[0]
    if off + 4 > len(buf):
        return None
    seed = struct.unpack_from("<I", buf, off)[0]
    src = buf[off + 4:off + 4 + size]
    rot = (T * ((len(src) >> 10) + 2))[seed & 0x3FF:][:len(src)]
    dec = (np.frombuffer(src.translate(BITREV), dtype=np.uint8) ^ np.frombuffer(rot, dtype=np.uint8)).tobytes()
    return dec if (zlib.adler32(dec) & 0xFFFFFFFF) == seed else None


def aud_decode(buf):
    return enc1_decode(buf) if buf[:4] == b"ENC1" else enc2_decode(buf)


def mov_to_m2v(path, out):
    """Decifra MOV3 / MOV2 (ou copia o MPEG puro do Exceed 2) para um .m2v."""
    with open(path, "rb") as f:
        hdr = f.read(0x8C)
        if hdr[:4] == b"\x00\x00\x01\xb3":
            f.seek(0)
            table = None
        elif hdr[:4] in (b"MOV3", b"MOV2"):
            n = struct.unpack_from("<I", hdr, 0x88)[0]
            extra = 0x10 if hdr[:4] == b"MOV3" else 0
            f.seek(0x8C + extra + n + 0x30)
            g = BITREV[f.read(1)[0]]
            table = bytes(BITREV[i] ^ g for i in range(256))
            f.seek(0x8C + extra + n + 0x34)
        else:
            raise ValueError("não é MOV2/MOV3/MPEG")
        with open(out, "wb") as o:
            while True:
                blk = f.read(1 << 22)
                if not blk:
                    break
                o.write(blk.translate(table) if table else blk)


def m2v_to_mov3(m2v, out, internal_name, rng):
    """Embrulha um MPEG-2 elementar no MOV3 do NX (piu 0x80a30e0 / 0x80a3380):
    hdr[0x8C] ("MOV3", 0x84 = tamanho do vídeo, 0x88 = N); A[16]; lixo[N]; B[16];
    nome[32] cifrado com k = 0x80a3dc0(A) ^ B (+0x54 a cada volta); G; vídeo em 0xD0+N,
    cada byte gravado como bitrev(b ^ bitrev(G))."""
    data = open(m2v, "rb").read()
    if data[:4] != b"\x00\x00\x01\xb3":
        raise ValueError("o vídeo gerado não começa com o sequence header do MPEG-2")
    # A precisa ter resposta na tabela do dongle do NX, senão o piu original não abre
    nx_q = sorted(q for q, (_, key) in DOG.items() if key == "nx_dog.key")
    A = nx_q[rng.integers(len(nx_q))] if nx_q else bytes(rng.integers(0, 256, 16, dtype=np.uint8))
    B = bytes(rng.integers(0, 256, 16, dtype=np.uint8))
    n = int(rng.integers(0x100, 0x380))
    G = int(rng.integers(1, 256))
    k = bytearray(a ^ b for a, b in zip(x2_key(A, dog_resp(A)), B))
    name = bytearray(internal_name.encode("latin1")[:31].ljust(32, b"\0"))
    for i in range(32):
        name[i] ^= k[i & 15]
        k[i & 15] = (k[i & 15] + 0x54) & 0xFF
    hdr = bytearray(rng.integers(0, 256, 0x8C, dtype=np.uint8).tobytes())
    hdr[0:4] = b"MOV3"
    struct.pack_into("<II", hdr, 0x84, len(data), n)
    g = BITREV[G]
    enc = bytes(BITREV[i ^ g] for i in range(256))
    with open(out, "wb") as o:
        o.write(hdr)
        o.write(A)
        o.write(rng.integers(0, 256, n, dtype=np.uint8).tobytes())
        o.write(B)
        o.write(name)
        o.write(bytes([G]) + rng.integers(0, 256, 3, dtype=np.uint8).tobytes())
        o.write(data.translate(enc))


# ───────────────────────────── arquivos e versões ─────────────────────────────
def version_num(name):
    m = re.match(r"(\d+)_", name)
    return int(m.group(1)) if m else -1


def find_roots(game):
    """[pasta do jogo, versões anteriores da mais próxima para a mais antiga]."""
    game = os.path.abspath(game)
    ver_dir = os.path.dirname(game)
    base = os.path.dirname(ver_dir)
    cur = version_num(os.path.basename(ver_dir))
    old = []
    if cur >= 0:
        for d in os.listdir(base):
            n = version_num(d)
            p = os.path.join(base, d, "game")
            if 0 <= n < cur and os.path.isdir(p):
                old.append((n, p))
    return [game] + [p for _, p in sorted(old, reverse=True)]


def first_existing(roots, rels):
    for r in roots:
        for rel in rels:
            p = os.path.join(r, rel)
            if os.path.exists(p):
                return p
    return None


def song_table():
    """[(id, baseId)] de src/nx_songs.c."""
    out = []
    p = os.path.join(HERE, "..", "src", "nx_songs.c")
    for line in open(p, encoding="utf-8"):
        m = re.match(r'\s*\{ 0x([0-9A-F]+), .*\}, (-1|0x[0-9A-F]+), \d', line)
        if m:
            out.append((m.group(1), None if m.group(2) == "-1" else m.group(2)[2:]))
    return out


# ───────────────────────────── áudio ─────────────────────────────
def decode_pcm(aud_path, tmp):
    raw = aud_decode(open(aud_path, "rb").read())
    if raw is None:
        raise ValueError("áudio não é ENC1/ENC2 ou não decifrou: " + aud_path)
    src = os.path.join(tmp, "a.bin")
    open(src, "wb").write(raw)
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", src, "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
                       capture_output=True)
    if r.returncode != 0 or not r.stdout:
        raise ValueError("ffmpeg não decodificou o áudio: " + r.stderr.decode(errors="replace")[:200])
    return np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32)


def locate(full, intro):
    """Instante (s) em que a intro se encaixa na música e a confiança do encaixe.
    Correlação cruzada normalizada pela energia local, sobre o miolo da intro (as pontas
    têm fade)."""
    cut = int(len(intro) * 0.15)
    core = intro[cut:len(intro) - cut]
    if len(core) < RATE or len(full) < len(core):
        return None, 0.0
    core = core - core.mean()
    full = full - full.mean()
    n = 1 << int(np.ceil(np.log2(len(full) + len(core))))
    corr = np.fft.irfft(np.fft.rfft(full, n) * np.conj(np.fft.rfft(core, n)), n)[:len(full) - len(core) + 1]
    c2 = np.concatenate(([0.0], np.cumsum(full.astype(np.float64) ** 2)))
    energy = np.sqrt(np.maximum(c2[len(core):] - c2[:-len(core)], 1e-9))[:len(corr)]
    score = corr / (energy * np.sqrt(float((core.astype(np.float64) ** 2).sum())) + 1e-9)
    best = int(np.argmax(score))
    return (best - cut) / RATE, float(score[best])


# ───────────────────────────── principal ─────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Import Old Data: cria as prévias que faltam em BGA/PREVIEW.")
    ap.add_argument("--game", default=os.path.join(HERE, "..", "..", "game"), help="pasta do jogo (NX)")
    ap.add_argument("--exe", help="executável do jogo para capturar os fundos .DAT "
                                  "(padrão: build/Debug/Pumpy.exe do projeto, senão NXTESTE.EXE da pasta do jogo)")
    ap.add_argument("--ids", help="só estes ids (hex, separados por vírgula)")
    ap.add_argument("--force", action="store_true", help="refaz mesmo se a prévia já existe")
    ap.add_argument("--dry-run", action="store_true", help="só calcula e lista; não grava nada")
    ap.add_argument("--min-score", type=float, default=0.30, help="confiança mínima do encaixe (0..1)")
    ap.add_argument("--report", help="grava a lista (id;status;inicio;duracao;confianca;fonte) neste arquivo")
    args = ap.parse_args()

    roots = find_roots(args.game)
    game, olds = roots[0], roots[1:]
    print("Jogo:", game)
    for o in olds:
        print("Versão anterior:", o)
    only = {x.strip().upper() for x in args.ids.split(",")} if args.ids else None
    exe = args.exe
    if not exe:
        cands = [os.path.join(HERE, "..", "build", "Debug", "Pumpy.exe"), os.path.join(game, "NXTESTE.EXE"),
                 os.path.join(game, "Pumpy.exe")]
        exe = next((os.path.abspath(c) for c in cands if os.path.exists(c)), None)
    rng = np.random.default_rng()
    prev_dir = os.path.join(game, "BGA", "PREVIEW")
    rows = []
    counts = {}

    def done(sid, status, t0=None, dur=None, score=None, src=""):
        counts[status] = counts.get(status, 0) + 1
        rows.append((sid, status, t0, dur, score, src))
        extra = "" if t0 is None else "  início %.2f s, duração %.2f s, confiança %.2f" % (t0, dur, score)
        print("%-6s %-22s%s  %s" % (sid, status, extra, src))

    for sid, base in song_table():
        if only and sid not in only:
            continue
        chain = [sid] + ([base] if base else [])
        if not args.force and any(os.path.exists(os.path.join(prev_dir, c + ".MOV")) for c in chain):
            continue                                   # o NX já tem (da própria ou da base)
        full = first_existing(roots, ["AUDIO/%s.AUD" % c for c in chain])
        intro = first_existing([game], ["AUDIO/INTRO/%s.AUD" % c for c in chain]) or \
            first_existing(olds, ["AUDIO/INTRO/%s.AUD" % c for c in chain] + ["AUDIO/D%s.AUD" % c for c in chain])
        if not full or not intro:
            done(sid, "sem áudio" if not full else "sem intro")
            continue
        mov = first_existing(roots, ["BGA/%s.MOV" % c for c in chain])
        bga_ids = [BGA_ALIAS[c] for c in chain if c in BGA_ALIAS] + chain
        dat = first_existing(roots, ["BGA/%s.DAT" % c for c in bga_ids])
        with tempfile.TemporaryDirectory() as tmp:
            try:
                pf, pi = decode_pcm(full, tmp), decode_pcm(intro, tmp)
            except ValueError as ex:
                done(sid, "erro de áudio", src=str(ex))
                continue
            t0, score = locate(pf, pi)
            dur = len(pi) / RATE
            if t0 is None or score < args.min_score:
                done(sid, "encaixe incerto", t0 or 0.0, dur, score, os.path.basename(intro))
                continue
            t0 = max(t0, 0.0)
            if not mov and not dat:
                done(sid, "sem fundo", t0, dur, score)
                continue
            if args.dry_run:
                done(sid, "pronta (vídeo)" if mov else "pronta (captura do .DAT)", t0, dur, score, mov or dat)
                continue
            m2v, cutp = os.path.join(tmp, "full.m2v"), os.path.join(tmp, "cut.m2v")
            scale = "scale=%d:%d" % (PREVIEW_W, PREVIEW_H)
            try:
                if mov:
                    mov_to_m2v(mov, m2v)
                    r = subprocess.run(
                        ["ffmpeg", "-v", "error", "-y", "-f", "mpegvideo", "-framerate", str(PREVIEW_FPS), "-i", m2v,
                         "-ss", "%.3f" % t0, "-t", "%.3f" % dur] + encode_args(scale) + [cutp],
                        capture_output=True)
                else:
                    if not exe:
                        raise ValueError("sem executável do jogo para capturar (use --exe)")
                    raw, w, h = capture_dat(exe, game, dat, t0, dur, tmp)
                    with open(raw, "rb") as rf:
                        rf.seek(8)
                        r = subprocess.run(
                            ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                             "-s", "%dx%d" % (w, h), "-framerate", str(PREVIEW_FPS), "-i", "-"]
                            + encode_args("vflip," + scale) + [cutp],
                            stdin=rf, capture_output=True)
                if r.returncode != 0 or not os.path.exists(cutp) or os.path.getsize(cutp) < 1000:
                    raise ValueError("ffmpeg: " + r.stderr.decode(errors="replace")[:200])
                os.makedirs(prev_dir, exist_ok=True)
                m2v_to_mov3(cutp, os.path.join(prev_dir, sid + ".MOV"), sid + ".M2V", rng)
            except (ValueError, OSError) as ex:
                done(sid, "erro de vídeo", t0, dur, score, str(ex))
                continue
            done(sid, "prévia criada" if mov else "prévia criada (.DAT)", t0, dur, score, mov or dat)

    print("\nResumo:", ", ".join("%s: %d" % kv for kv in sorted(counts.items())) or "nada a fazer")
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write("id;status;inicio_s;duracao_s;confianca;fonte\n")
            for sid, st, t0, dur, sc, src in rows:
                f.write("%s;%s;%s;%s;%s;%s\n" % (sid, st, "" if t0 is None else "%.3f" % t0,
                                                "" if dur is None else "%.3f" % dur,
                                                "" if sc is None else "%.3f" % sc, src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
