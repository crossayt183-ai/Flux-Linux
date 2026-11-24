import ast
import os
import re
import subprocess

p = r'F:\flux-linux\.github\workflows\main.yml'
with open(p, encoding='utf-8') as f:
    raw = f.read()

lines = raw.splitlines()
print('Lines:', len(lines), 'Chars:', len(raw))
tab_lines = [i for i, l in enumerate(lines, 1) if '\t' in l]
print('Tab lines:', tab_lines)
assert not tab_lines, f'Tabs on lines: {tab_lines}'

# Find Git bash
bash_candidates = [
    r'C:\Program Files\Git\bin\bash.exe',
    r'C:\Program Files\Git\usr\bin\bash.exe',
    'bash',
]
bash_bin = next((b for b in bash_candidates if os.path.exists(b) or b == 'bash'), 'bash')
print('Using bash:', bash_bin)

# Check YAML indentation & run blocks
run_blocks = []
i = 0
while i < len(lines):
    line = lines[i]
    m = re.match(r'^(\s+)run:\s*\|\s*$', line)
    if m:
        indent = len(m.group(1))
        block_indent = None
        start = i + 2
        buf = []
        i += 1
        while i < len(lines):
            l = lines[i]
            if l.strip() == '':
                buf.append('')
                i += 1
                continue
            cur_ind = len(l) - len(l.lstrip(' '))
            if cur_ind <= indent:
                break
            if block_indent is None:
                block_indent = cur_ind
            buf.append(l[block_indent:] if len(l) >= block_indent else l)
            i += 1
        run_blocks.append((start, '\n'.join(buf)))
        continue
    i += 1

print(f'Found {len(run_blocks)} run blocks')
for idx, (start, code) in enumerate(run_blocks, 1):
    res = subprocess.run([bash_bin, '-n'], input=code, text=True, encoding='utf-8', capture_output=True)
    print(f'  run[{idx}] line {start}: bash -n rc={res.returncode}')
    assert res.returncode == 0, res.stderr
    m_eof = re.search(r"sudo bash <<'EOF'\n(.*?)\nEOF\s*$", code, re.S | re.M)
    if m_eof:
        r2 = subprocess.run([bash_bin, '-n'], input=m_eof.group(1), text=True, encoding='utf-8', capture_output=True)
        print(f'    inner sudo bash <<EOF: rc={r2.returncode}')
        assert r2.returncode == 0, r2.stderr
        m_chr = re.search(r"chroot \"\$ROOTFS\" /bin/bash -e <<'CHROOT_EOF'\n(.*?)\nCHROOT_EOF", m_eof.group(1), re.S)
        if m_chr:
            r3 = subprocess.run([bash_bin, '-n'], input=m_chr.group(1), text=True, encoding='utf-8', capture_output=True)
            print(f'    inner CHROOT_EOF: rc={r3.returncode}')
            assert r3.returncode == 0, r3.stderr

# Check permissions section indentation & YAML key sanity
head = '\n'.join(lines[:20])
assert 'pages: write' in head and 'id-token: write' in head and 'contents: read' in head
print('Permissions block verified OK')

# Check embedded python heredocs compile cleanly
py_blocks = re.findall(r"(?:python3 - <<'PY'|cat > .*? <<'(?:FLUXWELCOME|FLUXSEC|FLUXSTORE|FLUXDASH|FLUXHUD|PY)')\n(.*?)\n(?:PY|FLUXWELCOME|FLUXSEC|FLUXSTORE|FLUXDASH|FLUXHUD)", raw, re.S)
for k, pb in enumerate(py_blocks, 1):
    # strip common indentation
    non_empty = [l for l in pb.splitlines() if l.strip()]
    min_ind = min(len(l) - len(l.lstrip(' ')) for l in non_empty)
    dedented = '\n'.join(l[min_ind:] if len(l) >= min_ind else '' for l in pb.splitlines())
    if dedented.lstrip().startswith('#!') or 'import ' in dedented[:300]:
        ast.parse(dedented)
        print(f'  python AST[{k}] lines={len(dedented.splitlines())}: OK')

print('ALL VALIDATIONS PASSED')
