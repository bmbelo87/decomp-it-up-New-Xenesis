import sys
import os

sys.path.append('tools')
import analyze_see_and_fire as a
import zero_decrypt

step_path = os.path.abspath(r'..\game\STEP.DAT')
with open(step_path, 'rb') as f:
    step_data = f.read()

entries = {}
for name, content in zero_decrypt.respac2(step_data):
    if name.upper().endswith('.SEE'):
        entries[name.upper()] = content

targets = [
    'AA271', 'AA512', 'FFC04', 'C23', 'C26', 'A-25', 'A101', 'B902',
    'B501', 'C28', 'C25', 'C182', '2516', 'C32'
]

print("Detalhes dos charts para adição ao Another:")
for t in targets:
    fn = t + '.SEE'
    if fn in entries:
        sig = a.analyze_see(entries[fn])
        secs = []
        for si, s in sorted(sig.items()):
            if s['total_steps'] > 0:
                secs.append(f"{s['name']}(s{si})={s['total_steps']}st")
        print(f"{t:8s}: {', '.join(secs)}")
    else:
        print(f"{t:8s}: NÃO ENCONTRADO")
