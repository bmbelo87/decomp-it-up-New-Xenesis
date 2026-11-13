# PyInstaller: assistente Old Data (tools/old_data_wizard.py) -> OldDataWizard.exe
#   cd tools && python -m PyInstaller --noconfirm old_data_wizard.spec
# O ffmpeg.exe NÃO vai dentro do .exe (são ~150 MB descompactados a cada abertura): vai ao lado,
# na mesma pasta do release (import_old_data.py procura ao lado do .exe antes do PATH).
a = Analysis(
    ['old_data_wizard.py'],
    pathex=['.'],
    datas=[
           ('data/zero_enc1_table.bin', 'data'), ('data/songs.txt', 'data')],   # SEM as chaves do dongle
    hiddenimports=['import_old_data', 'respack_extract', 'old_data_i18n'],
    excludes=['matplotlib', 'scipy', 'pandas', 'PIL', 'IPython', 'pytest'],
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz, a.scripts, a.binaries, a.datas, [],
    name='OldDataWizard',
    console=False,
    upx=False,
)
