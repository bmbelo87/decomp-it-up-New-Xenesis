#!/usr/bin/env python3
"""Assistente "Old Data" — acompanha o Pumpy.exe no release.

Janela gráfica que explica o que a pessoa precisa ter, mostra a árvore de pastas que o jogo
procura (a mesma regra de src/old_data.c), confere o que existe em cada versão e roda o
tools/import_old_data.py para criar as prévias (BGA/PREVIEW/<ID>.MOV) que faltam.
Idioma da janela: seletor no topo (textos em tools/old_data_i18n.py); a escolha fica em
OldDataWizard.ini ao lado do programa.

Não traz nenhum arquivo do jogo: usa só o que a pessoa já tem instalado.
Empacotado com PyInstaller (tools/old_data_wizard.spec); também roda direto: python old_data_wizard.py
"""
import io
import os
import queue
import shutil
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

if sys.stdout is None:                       # .exe sem console (--windowed)
    sys.stdout = io.StringIO()
if sys.stderr is None:
    sys.stderr = io.StringIO()

import import_old_data as iod                # noqa: E402  (mesma pasta; no .exe vai embutido)
from old_data_i18n import LANGS, T, get_lang, set_lang   # noqa: E402

CREATE_NO_WINDOW = 0x08000000
_real_run = subprocess.run


def _run(*a, **k):
    """ffmpeg não pode abrir uma janela de console por música; sem console o stdin precisa ser explícito."""
    if os.name == "nt":
        k.setdefault("creationflags", CREATE_NO_WINDOW)
    if "input" not in k and "stdin" not in k:
        k["stdin"] = subprocess.DEVNULL
    return _real_run(*a, **k)


subprocess.run = _run

GAME_EXES = ("Pumpy.exe", "PUMPY.EXE", "NXTESTE.EXE")
APP_DIR = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
INI = os.path.join(APP_DIR, "OldDataWizard.ini")


def exists(*p):
    return os.path.exists(os.path.join(*p))


# ───────────── idioma ─────────────
def system_lang():
    """'pt' se o Windows está em português, senão 'en'."""
    try:
        import ctypes
        buf = ctypes.create_unicode_buffer(85)
        ctypes.windll.kernel32.GetUserDefaultLocaleName(buf, 85)
        if buf.value.lower().startswith("pt"):
            return "pt"
    except Exception:
        pass
    return "en"


def load_lang():
    try:
        for line in open(INI, encoding="utf-8"):
            k, _, v = line.strip().partition("=")
            if k == "lang" and v in dict(LANGS):
                return v
    except OSError:
        pass
    return system_lang()


def save_lang(code):
    try:
        open(INI, "w", encoding="utf-8").write("lang=%s\n" % code)
    except OSError:
        pass                                  # pasta sem permissão de escrita: só não lembra


# ───────────── conferência ─────────────
def default_game_dir():
    """Pasta do .exe do assistente, se for a do jogo (tem AUDIO e BGA); senão vazio."""
    for d in (APP_DIR, os.getcwd()):
        if exists(d, "AUDIO") and exists(d, "BGA"):
            return d
    return ""


def find_game_exe(game):
    for n in GAME_EXES:
        if exists(game, n):
            return os.path.join(game, n)
    return None


def check_tree(game):
    """[(nivel, chave, args)]: nivel ok / warn / err; o texto sai de T(chave, *args) na hora de mostrar."""
    out = []
    game = os.path.abspath(game)
    if not os.path.isdir(game):
        return [("err", "k_nodir", (game,))]
    for sub in ("AUDIO", "BGA", "TITLE"):
        out.append(("ok", "k_sub_ok", (sub,)) if exists(game, sub) else ("err", "k_sub_err", (sub,)))
    out.append(("ok", "k_step_ok", ()) if exists(game, "STEP.DAT") or exists(game, "STEP")
               else ("warn", "k_step_warn", ()))
    exe = find_game_exe(game)
    out.append(("ok", "k_exe_ok", (os.path.basename(exe),)) if exe else ("warn", "k_exe_warn", ()))
    ver_dir = os.path.dirname(game)
    cur = iod.version_num(os.path.basename(ver_dir))
    if cur < 0:
        out.append(("err", "k_ver_bad", (os.path.basename(ver_dir),)))
        return out
    out.append(("ok", "k_ver_ok", (cur, os.path.basename(ver_dir))))
    roots = iod.find_roots(game)[1:]
    base = os.path.dirname(ver_dir)
    if not roots:
        out.append(("err", "k_noold", (base, os.path.join(base, "17_zero"))))
    for r in roots:
        name = os.path.basename(os.path.dirname(r))
        miss = [s for s in ("AUDIO", "BGA", "TITLE") if not exists(r, s)]
        out.append(("warn", "k_old_miss", (name, ", ".join(miss))) if miss else ("ok", "k_old_ok", (name,)))
    # pastas NN_nome com NN menor mas sem "game" dentro: erro comum
    if os.path.isdir(base):
        for d in sorted(os.listdir(base)):
            n = iod.version_num(d)
            if 0 <= n < cur and not os.path.isdir(os.path.join(base, d, "game")):
                out.append(("warn", "k_old_nogame", (d,)))
    # a decifragem é feita pelo próprio Pumpy.exe (--dog-serve, src/dog_serve.c): nenhum arquivo de chave
    # é distribuído. Os arquivos .key em data/ ficam só como reserva (uso de desenvolvimento).
    n = iod.dog_probe(exe) if exe else 0
    if n:
        out.append(("ok", "k_dog_ok", (n,)))
    elif len(iod.reload_dog()) >= len(iod.KEY_FILES):
        out.append(("warn", "k_dog_files", ()))
    else:
        out.append(("err", "k_dog_err", ()))
    ff = iod.FFMPEG
    ok = os.path.isfile(ff) or shutil.which(ff)
    out.append(("ok", "k_ff_ok", (ff,)) if ok else ("err", "k_ff_err", ()))
    return out


def analyze(game):
    """Linhas (id, áudio, intro, fundo, prévia, estado, fonte) como o import_old_data decide.
    estado: have / noaudio / nointro / nobg / gen (traduzido na exibição: st_<estado>)."""
    roots = iod.find_roots(game)
    olds = roots[1:]
    prev_dir = os.path.join(os.path.abspath(game), "BGA", "PREVIEW")
    rows = []
    for sid, base in iod.song_table():
        chain = [sid] + ([base] if base else [])
        has_prev = any(os.path.exists(os.path.join(prev_dir, c + ".MOV")) for c in chain)
        full = iod.first_existing(roots, ["AUDIO/%s.AUD" % c for c in chain])
        intro = iod.first_existing([roots[0]], ["AUDIO/INTRO/%s.AUD" % c for c in chain]) or \
            iod.first_existing(olds, ["AUDIO/INTRO/%s.AUD" % c for c in chain] + ["AUDIO/D%s.AUD" % c for c in chain])
        mov = iod.first_existing(roots, ["BGA/%s.MOV" % c for c in chain])
        bga_ids = [iod.BGA_ALIAS[c] for c in chain if c in iod.BGA_ALIAS] + chain
        dat = iod.first_existing(roots, ["BGA/%s.DAT" % c for c in bga_ids])
        if has_prev:
            state = "have"
        elif not full:
            state = "noaudio"
        elif not intro:
            state = "nointro"
        elif not (mov or dat):
            state = "nobg"
        else:
            state = "gen"
        src = ""
        if state != "have" and full:       # <versão>/game/AUDIO/<ID>.AUD -> nome da pasta <versão>
            src = os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(full))))
        rows.append((sid, bool(full), bool(intro), "MOV" if mov else ("DAT" if dat else ""), has_prev, state, src))
    return rows


class StdoutToQueue(io.TextIOBase):
    """Recebe os print() do import_old_data; cancelar = levantar uma exceção no próximo print."""

    def __init__(self, q):
        self.q, self.cancel = q, False

    def write(self, s):
        if self.cancel:
            raise InterruptedError("cancelado")
        if s:
            self.q.put(("log", s))
        return len(s)

    def flush(self):
        pass


class Wizard(tk.Tk):
    def __init__(self):
        super().__init__()
        set_lang(load_lang())
        self.geometry("940x660")
        self.minsize(820, 560)
        self.q = queue.Queue()
        self.worker = None
        self.out = None
        self.rows = []
        self.n_gen = 0
        self.done_n = 0
        self.log_text = ""
        self.game = tk.StringVar(value=default_game_dir())
        self.force = tk.BooleanVar(value=False)
        self.dry = tk.BooleanVar(value=False)
        self.show_all = tk.BooleanVar(value=False)
        self.ids = tk.StringVar()
        self.lang_var = tk.StringVar()
        self.nb = None
        self.build()
        self.after(100, self._pump)

    # ───────────── montagem (refeita ao trocar de idioma) ─────────────
    def build(self, tab=0):
        if self.nb is not None:
            self.log_text = self.log.get("1.0", "end-1c")
            self.nb.destroy()
            self.bar.destroy()
        self.title(T("title"))
        self.bar = ttk.Frame(self)
        self.bar.pack(fill="x", padx=8, pady=(8, 0))
        ttk.Label(self.bar, text=T("lang")).pack(side="right", padx=(6, 0))
        names = [n for _, n in LANGS]
        self.lang_var.set(dict(LANGS)[get_lang()])
        self.cb_lang = ttk.Combobox(self.bar, values=names, textvariable=self.lang_var, state="readonly", width=20)
        self.cb_lang.pack(side="right")
        self.cb_lang.bind("<<ComboboxSelected>>", self.on_lang)
        nb = self.nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=8, pady=8)
        self._text_tab(nb, T("tab1"), T("intro"))
        self._build_folders(nb)
        self._build_songs(nb)
        self._build_run(nb)
        nb.select(tab)
        self.log.insert("1.0", self.log_text)
        self.log.see("end")
        if self.worker and self.worker.is_alive():
            self.btn_go.config(state="disabled")
            self.btn_stop.config(state="normal")
            self.cb_lang.config(state="disabled")
        self.prog.config(maximum=max(1, self.n_gen), value=self.done_n)
        self.prog_lbl.config(text=str(self.done_n) if self.done_n else "")
        self.refresh()

    def on_lang(self, _e=None):
        code = next(c for c, n in LANGS if n == self.lang_var.get())
        if code == get_lang():
            return
        set_lang(code)
        save_lang(code)
        self.build(self.nb.index(self.nb.select()))

    def _text_tab(self, nb, title, text):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=title)
        t = tk.Text(f, wrap="word", font=("Segoe UI", 10), relief="flat", padx=6, pady=6)
        t.insert("1.0", text)
        t.config(state="disabled")
        t.pack(fill="both", expand=True)

    def _build_folders(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=T("tab2"))
        top = ttk.Frame(f)
        top.pack(fill="x")
        ttk.Label(top, text=T("game_folder")).pack(side="left")
        ttk.Entry(top, textvariable=self.game).pack(side="left", fill="x", expand=True, padx=6)
        ttk.Button(top, text=T("browse"), command=self.browse).pack(side="left")
        ttk.Button(top, text=T("verify"), command=self.refresh).pack(side="left", padx=(6, 0))
        pan = ttk.PanedWindow(f, orient="horizontal")
        pan.pack(fill="both", expand=True, pady=(10, 0))
        left = ttk.LabelFrame(pan, text=T("expected"), padding=6)
        right = ttk.LabelFrame(pan, text=T("found"), padding=6)
        pan.add(left, weight=3)
        pan.add(right, weight=2)
        t = tk.Text(left, wrap="none", font=("Consolas", 9), relief="flat")
        t.insert("1.0", T("tree"))
        t.config(state="disabled")
        t.pack(fill="both", expand=True)
        self.check = tk.Text(right, wrap="word", font=("Segoe UI", 9), relief="flat", width=40)
        self.check.tag_config("ok", foreground="#1b7a2b")
        self.check.tag_config("warn", foreground="#a86400")
        self.check.tag_config("err", foreground="#b3261e")
        self.check.config(state="disabled")
        self.check.pack(fill="both", expand=True)

    def _build_songs(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=T("tab3"))
        top = ttk.Frame(f)
        top.pack(fill="x")
        self.summary = ttk.Label(top, text=T("sum_none"))
        self.summary.pack(side="left")
        ttk.Checkbutton(top, text=T("show_all"), variable=self.show_all, command=self.fill_songs).pack(side="right")
        cols = ("id", "audio", "intro", "fundo", "previa", "estado", "fonte")
        heads = [T(k) for k in ("c_id", "c_audio", "c_intro", "c_bg", "c_prev", "c_state", "c_src")]
        wid = (70, 60, 60, 70, 60, 150, 140)
        self.tree = ttk.Treeview(f, columns=cols, show="headings")
        for c, h, w in zip(cols, heads, wid):
            self.tree.heading(c, text=h)
            self.tree.column(c, width=w, anchor="w")
        sb = ttk.Scrollbar(f, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True, pady=(8, 0))
        sb.pack(side="left", fill="y", pady=(8, 0))
        self.tree.tag_configure("gen", foreground="#1b7a2b")
        self.tree.tag_configure("falta", foreground="#b3261e")

    def _build_run(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=T("tab4"))
        opt = ttk.Frame(f)
        opt.pack(fill="x")
        ttk.Checkbutton(opt, text=T("o_force"), variable=self.force).pack(side="left")
        ttk.Checkbutton(opt, text=T("o_dry"), variable=self.dry).pack(side="left", padx=12)
        ttk.Label(opt, text=T("o_ids")).pack(side="left", padx=(12, 4))
        ttk.Entry(opt, textvariable=self.ids, width=22).pack(side="left")
        bar = ttk.Frame(f)
        bar.pack(fill="x", pady=8)
        self.btn_go = ttk.Button(bar, text=T("start"), command=self.start)
        self.btn_go.pack(side="left")
        self.btn_stop = ttk.Button(bar, text=T("cancel"), command=self.stop, state="disabled")
        self.btn_stop.pack(side="left", padx=6)
        self.prog = ttk.Progressbar(bar, mode="determinate")
        self.prog.pack(side="left", fill="x", expand=True, padx=8)
        self.prog_lbl = ttk.Label(bar, text="")
        self.prog_lbl.pack(side="left")
        self.log = tk.Text(f, wrap="word", font=("Consolas", 9), height=20)
        self.log.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(f, orient="vertical", command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side="left", fill="y")

    # ───────────── ações ─────────────
    def browse(self):
        d = filedialog.askdirectory(title=T("browse_title"), initialdir=self.game.get() or None)
        if d:
            self.game.set(os.path.normpath(d))
            self.refresh()

    def refresh(self):
        game = self.game.get().strip()
        self.check.config(state="normal")
        self.check.delete("1.0", "end")
        if not game:
            self.check.insert("end", T("pick_game") + "\n", "warn")
        else:
            for lvl, key, args in check_tree(game):
                self.check.insert("end", {"ok": "[ok] ", "warn": "[!] ", "err": "[X] "}[lvl] + T(key, *args) + "\n\n", lvl)
        self.check.config(state="disabled")
        self.rows = []
        err = None
        if game and os.path.isdir(game):
            try:
                self.rows = analyze(game)
            except Exception as ex:                      # pasta fora do padrão: a aba 2 já avisa
                err = str(ex)
        self.fill_songs()
        if err:
            self.summary.config(text=T("sum_err", err))

    def fill_songs(self):
        self.tree.delete(*self.tree.get_children())
        n_gen = n_miss = 0
        for r in self.rows:
            st = r[5]
            if st == "gen":
                n_gen += 1
            elif st != "have":
                n_miss += 1
            if st == "have" and not self.show_all.get():
                continue
            yes = T("yes")
            vals = (r[0], yes if r[1] else "—", yes if r[2] else "—", r[3] or "—", yes if r[4] else "—",
                    T("st_" + st), r[6])
            self.tree.insert("", "end", values=vals, tags=("gen" if st == "gen" else ("falta" if st != "have" else ""),))
        if self.rows:
            self.summary.config(text=T("sum", n_gen, n_miss))
        else:
            self.summary.config(text=T("sum_none"))
        self.n_gen = n_gen

    def start(self):
        game = self.game.get().strip()
        if not game or not os.path.isdir(game):
            messagebox.showwarning(T("title"), T("pick_game_first"))
            self.nb.select(1)
            return
        exe = find_game_exe(game)
        if not (exe and iod.dog_probe(exe)) and len(iod.reload_dog()) < len(iod.KEY_FILES):
            messagebox.showwarning(T("title"), T("need_keys"))
            self.nb.select(1)
            return
        argv = ["--game", game]
        if exe:
            argv += ["--exe", exe]
        if self.force.get():
            argv.append("--force")
        if self.dry.get():
            argv.append("--dry-run")
        if self.ids.get().strip():
            argv += ["--ids", self.ids.get().strip()]
        self.log.delete("1.0", "end")
        self.prog.config(value=0, maximum=max(1, self.n_gen))
        self.prog_lbl.config(text="0")
        self.done_n = 0
        self.btn_go.config(state="disabled")
        self.btn_stop.config(state="normal")
        self.cb_lang.config(state="disabled")             # trocar o idioma refaz a janela: só fora da execução
        self.out = StdoutToQueue(self.q)
        self.worker = threading.Thread(target=self._work, args=(argv,), daemon=True)
        self.worker.start()

    def stop(self):
        if self.out:
            self.out.cancel = True
            self.btn_stop.config(state="disabled")
            self.log.insert("end", T("canceling"))

    def _work(self, argv):
        old = sys.stdout
        sys.stdout = self.out
        try:
            iod.main(argv)
            self.q.put(("end", "r_done"))
        except InterruptedError:
            self.q.put(("end", "r_canceled"))
        except SystemExit as ex:
            self.q.put(("end", ("r_exit", ex.code)))
        except Exception as ex:                              # mostra no log em vez de morrer calado
            self.q.put(("end", ("r_err", ex)))
        finally:
            sys.stdout = old

    def _pump(self):
        try:
            while True:
                kind, s = self.q.get_nowait()
                if kind == "log":
                    self.log.insert("end", s)
                    self.log.see("end")
                    for line in s.splitlines():
                        tok = line.split(None, 1)[:1]
                        if tok and any(r[0] == tok[0] for r in self.rows):
                            self.done_n += 1
                            self.prog.config(value=self.done_n)
                            self.prog_lbl.config(text=str(self.done_n))
                else:
                    key, args = (s, ()) if isinstance(s, str) else (s[0], (s[1],))
                    self.log.insert("end", "\n" + T(key, *args) + "\n")
                    self.log.see("end")
                    self.btn_go.config(state="normal")
                    self.btn_stop.config(state="disabled")
                    self.cb_lang.config(state="readonly")
                    if key == "r_done":
                        self.refresh()
        except queue.Empty:
            pass
        self.after(100, self._pump)


def main():
    Wizard().mainloop()


if __name__ == "__main__":
    main()
