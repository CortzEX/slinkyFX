#!/usr/bin/env python3
"""Compare the SysV (Linux) register assignment of every extern "C" definition with every declaration
of the same symbol, and report the ones where a caller and the callee would disagree.

Why this is needed: the engine was recovered from MSVC machine code.  In the Microsoft x64 convention a
pointer, an int, an unsigned long long and an 8-byte struct all arrive in the same integer register and
all return in RAX, so the recovered prototypes can spell one function five different ways and still run.
Under SysV those spellings land in different places (RAX versus XMM0 versus the stack versus a hidden
pointer that shifts every later parameter), so each spelling difference is a live bug on Linux.
"""
import os, re, sys, collections

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'src'
SCALAR = {'void': 0, 'char': 1, 'signed char': 1, 'unsigned char': 1, 'short': 2, 'unsigned short': 2,
          'int': 4, 'unsigned': 4, 'unsigned int': 4, 'signed': 4, 'float': 4, 'double': 8,
          'long': 8, 'unsigned long': 8, 'long long': 8, 'unsigned long long': 8, 'size_t': 8,
          'ssize_t': 8, 'ptrdiff_t': 8, 'wchar_t': 2, 'bool': 1, 'jboolean': 1, 'jbyte': 1, 'jchar': 2,
          'jshort': 2, 'jint': 4, 'jlong': 8, 'jfloat': 4, 'jdouble': 8, 'time_t': 8, 'clock_t': 8,
          'int8_t': 1, 'uint8_t': 1, 'int16_t': 2, 'uint16_t': 2, 'int32_t': 4, 'uint32_t': 4,
          'int64_t': 8, 'uint64_t': 8, 'uintptr_t': 8, 'intptr_t': 8, 'cJSON_bool': 4, 'va_list': 8}
EXTERN = set('''jobject jclass jstring jthrowable jarray jbyteArray JNIEnv JvmtiEnv cJSON cJSON void* HANDLE HWND
LPARAM WPARAM DWORD BOOL UINT HMODULE HINSTANCE LPVOID COLORREF ATOM HFONT HBITMAP HACCEL HICON HGDIOBJ
POINT RECT SIZE ImU32 ImDrawIdx ImTextureID ImS8 ImU8 ImS16 ImU16 ImS32 ImU32 ImS64 ImU64 ImWchar off_t
mode_t uid_t gid_t pid_t socklen_t ino_t dev_t nlink_t blksize_t timespec timeval stat Dl_info siginfo_t
u128 sigset_t pthread_t'''.split())

files = []
for dp, dirs, fs in os.walk(ROOT):
    dirs[:] = [d for d in dirs if d not in ('.git', 'build')]
    for f in fs:
        if f.endswith(('.cpp', '.h', '.hpp')): files.append(os.path.join(dp, f))

# ---------- struct layout ----------
struct_re = re.compile(r'^[ \t]*(?:typedef[ \t]+)?struct[ \t]+([A-Za-z_]\w*)[^;{}]*\{', re.M)
def find_defs(text):
    """name -> (body lines, dtor?) for every struct definition in a file."""
    lines = text.split('\n')
    out = []
    for i, line in enumerate(lines):
        m = struct_re.match(line)
        if not m: continue
        depth = line.count('{') - line.count('}'); body = [line]; j = i
        while depth > 0 and j + 1 < len(lines):
            j += 1; body.append(lines[j]); depth += lines[j].count('{') - lines[j].count('}')
        out.append((m.group(1), body, bool(re.search(r'~\s*' + m.group(1) + r'\s*\(', '\n'.join(body)))))
    return out

members = {}      # name -> (list of member types, has_destructor)
layout_conflicts = []
for p in files:
    for name, body, dtor in find_defs(open(p, encoding='utf-8', errors='ignore').read()):
        inner = '\n'.join(body)
        inner = inner[inner.index('{') + 1: inner.rindex('}') if '}' in inner else None]
        inner = re.sub(r'//.*', '', inner)
        inner = re.sub(r'^[ \t]*#.*$', '', inner, flags=re.M)      # preprocessor is not a member
        inner = re.sub(r'~\s*[A-Za-z_]\w*\s*(\([^)]*\))?\s*(const)?\s*\{[^{}]*\}', '', inner)  # dtor marker
        parts, depth, cur = [], 0, ''
        for ch in inner:
            if ch in '([': depth += 1
            elif ch in ')]': depth -= 1
            if ch == ',' and depth == 0: parts.append(cur); cur = ''
            else: cur += ch
        if cur.strip(): parts.append(cur)
        ms, prev_t = [], None
        for part in parts:
            part = part.strip().replace('const', '').replace('volatile', '')
            part = re.sub(r'[;,]+$', '', part.strip()).strip()
            if not part or part.startswith('~') or part.startswith('#'): continue
            if '(' in part and ')' in part and '(*' in part: ms.append(('fnptr', 1)); continue
            cnt = 1
            am = re.search(r'\[(\w*)\]', part)
            if am:
                cnt = int(am.group(1), 0) if am.group(1).isdigit() else 0
                part = part[:am.start()] + part[am.end():]
            bare = re.fullmatch(r'[A-Za-z_]\w*', part.strip())
            if bare and prev_t and not re.match(r'(unsigned|signed)\b', prev_t):
                ms.append((prev_t, cnt)); continue
            if '*' in part: ms.append(('ptr', cnt)); prev_t = None; continue
            toks = part.replace('*', ' * ').split()
            if len(toks) == 1:
                t, nm = toks[0], ''
            else:
                t = ' '.join(toks[:-1]); nm = toks[-1]
            t = t.replace('struct ', '').strip()
            if t in ('unsigned', 'signed', 'long', 'short'): t = ' '.join(toks[:2]) if len(toks) > 1 else 'int'
            if re.fullmatch(r'[A-Za-z_]\w*', nm): prev_t = t
            ms.append((t, cnt))
        if name in members and members[name][0] != ms:
            layout_conflicts.append((name, str(members[name][0]), str(ms)))
        members.setdefault(name, (ms, dtor))
        if members[name][1] != dtor and not dtor: members[name] = (members[name][0], dtor)

def layout(name, seen=()):
    """(size, align, [eightbyte classes]) or None when the type is unknown."""
    if name in SCALAR:
        s = SCALAR[name]; return s, s, ['INT' if name not in ('float', 'double') else 'SSE'] * max(1, s // 8 or 1)
    if name in EXTERN: return 8, 8, ['INT']
    if name in seen or name not in members: return None
    ms, dtor = members[name]
    size = align = 0; flat = []
    for t, cnt in ms:
        if t == 'ptr': sz, al, cls = 8, 8, ['INT'] * cnt
        elif t == 'fnptr': sz, al, cls = 8, 8, ['INT'] * cnt
        else:
            one = layout(t, seen + (name,))
            if one is None: return None
            sz, al, cls = one
            sz, cls = sz * max(cnt, 1), cls * max(cnt, 1)
            cnt = 1
        if cnt == 0: return None
        align = max(align, al); size = (size + al - 1) // al * al + sz; flat += cls
    size = (size + align - 1) // align * align
    return size, max(align, 1), flat

def jni_norm(s):
    s = re.sub(r'\b(jfloat|jdouble|jboolean|jbyte|jchar|jshort|jint|jlong)\b',
               lambda m: {'jfloat': 'float', 'jdouble': 'double', 'jboolean': 'unsigned char',
                          'jbyte': 'signed char', 'jchar': 'unsigned short', 'jshort': 'short',
                          'jint': 'int', 'jlong': 'long long'}[m.group(1)], s)
    return re.sub(r'\b(jclass|jstring|jarray|jobject[A-Za-z]*|jthrowable|JNIEnv|va_list)\b', 'void *', s)

def classes(spelling):
    """SysV eightbyte classes for one parameter or return type spelling, or None if unknown."""
    s = jni_norm(spelling.replace('const', '').replace('volatile', '').strip())
    if '*' in s or '[' in s: return ('INT', 8, False)
    s = re.sub(r'\[.*', '', s).strip()
    if not s or s == 'void': return ('VOID', 0, False)
    if s in SCALAR:
        sz, al, cls = layout(s)
        return (('SSE' if s in ('float', 'double') else 'INT'), sz, False)
    if s in EXTERN: return ('INT', 8, False)
    if s not in members: return None
    l = layout(s)
    if l is None: return None
    size, align, flat = l
    nontrivial = members[s][1]
    if nontrivial: return ('INDIRECT', size, True)
    if size > 16: return ('MEMORY', size, True)
    kinds = set(flat)
    if kinds == {'SSE'}: return ('SSE', size, False)
    if kinds == {'INT'} or not kinds: return ('INT', size, False)
    return ('MEMORY', size, True)

# ---------- signatures ----------
FN_RE = re.compile(r'\bFN\s*\(\s*([^,()]*?)\s*,\s*([A-Za-z_]\w*)\s*,\s*\(([^()]*)\)\s*\)')
EXT = re.compile(r'extern\s+"C"[^{;=]*?\b([A-Za-z_]\w*)\s*\(([^{};]*)\)\s*(\{|;)')
def parse(text):
    for m in FN_RE.finditer(text):
        yield m.group(2), m.group(1), m.group(3), text[:m.start()].count('\n') + 1, False
    for m in EXT.finditer(text):
        head = text[m.start():m.start(1)].replace('extern "C"', '')
        head = re.sub(r'__attribute__\s*\(\([^()]*(?:\([^()]*\)[^()]*)*\)\)', '', head)
        yield m.group(1), head.strip(), m.group(2), text[:m.start()].count('\n') + 1, m.group(3) == '{'

def sig(ret, args):
    """(return class, register assignment string) or None."""
    rsp = jni_norm(ret.strip())
    r = ('INT', 8, False) if '*' in rsp else classes(rsp)
    if r is None: return None
    if args.strip() == 'void': argl = []
    else:
        argl = [a for a in args.split(',') if a.strip()]
    cls = []
    for a in argl:
        raw = jni_norm(a.strip())
        if '*' in raw or raw.endswith('[]'):
            cls.append(('ADDR', 8, False)); continue
        base = re.sub(r'\s+', ' ', raw.replace('const', '').replace('volatile', '')).strip()
        m = re.fullmatch(r'(.+?[A-Za-z_0-9}>])(?:\s+([A-Za-z_]\w*))?', base)
        if m and '*' not in base: base = m.group(1).strip()
        cls.append(classes(base))
    if any(c is None for c in cls): return None
    gr = ['rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9']; xr = ['xmm%d' % i for i in range(8)]
    hidden = r[2] or r[0] == 'MEMORY'
    if hidden and r[0] != 'VOID':
        pass
    elif r[0] == 'VOID':
        hidden = False
    gi = xi = 0
    if hidden and r[0] != 'VOID': gi = 1          # rdi holds the return buffer
    if r[0] == 'INDIRECT': gi = 1
    out = ['ret:' + r[0]]
    for i, c in enumerate(cls):
        if c[0] == 'ADDR' or c[2]:
            out.append('a%d=*%s' % (i, gr[gi] if gi < 6 else 'stack')); gi += 1
        elif c[0] == 'INT':
            out.append('a%d=%s' % (i, gr[gi] if gi < 6 else 'stack')); gi += 1
        elif c[0] == 'SSE':
            out.append('a%d=%s' % (i, xr[xi] if xi < 8 else 'stack')); xi += 1
        elif c[0] == 'MEMORY':
            out.append('a%d=stack' % i)
        elif c[0] == 'INDIRECT':
            out.append('a%d=%s' % (i, gr[gi] if gi < 6 else 'stack')); gi += 1
        else:
            out.append('a%d=void' % i)
    return ' '.join(out)

unmarked = []
def check_unmarked(name, ret, args, where):
    for sp in [ret] + [a for a in args.split(',') if a.strip()]:
        s = jni_norm(sp.strip()).replace('const', '').replace('volatile', '').strip()
        s = re.sub(r'\s+[A-Za-z_]\w*$', '', s)
        if '*' in s or '[' in s or s in SCALAR or s in EXTERN or not s: continue
        l = layout(s) if s in members else None
        if l is None: continue
        sz, al, flat = l
        if members[s][1] or sz <= 8: continue
        unmarked.append(f'{name} at {where}: {s} ({sz} bytes, passed by registers on SysV, by address on MS)')

defs, decls = {}, collections.defaultdict(list)
skipped, decl_blind = [], []
for p in files:
    text = open(p, encoding='utf-8', errors='ignore').read()
    for name, ret, args, line, is_def in parse(text):
        s = sig(ret, args)
        check_unmarked(name, ret, args, f'{p}:{line}')
        if s is None:
            (skipped if is_def else decl_blind).append((name, f'{p}:{line}', f'{ret} ({args})'))
            continue
        if is_def and name not in defs: defs[name] = (f'{p}:{line}', s)
        elif not is_def: decls[name].append((f'{p}:{line}', s))

blind = 0
bad, benign = [], []
for name in sorted(set(defs) | set(decls)):
    rows = ([defs[name]] if name in defs else []) + list(decls.get(name, []))
    sigs = {s for _, s in rows if not s.startswith('ret:VOID')}
    ref = defs.get(name, rows[0])
    for (cp, cs) in rows:
        if cs.startswith('ret:VOID') and not ref[1].startswith(('ret:INDIRECT', 'ret:MEMORY')):
            continue   # ignoring a scalar register return cannot go wrong
        if cs != ref[1]:
            if cs.replace('=*', '=') == ref[1].replace('=*', '='):
                benign.append((name, ref[0], ref[1], cp, cs))   # same registers; a handle is still a 64-bit value
                continue
            bad.append((name, ref[0], ref[1], cp, cs))
print(f'symbols with a definition: {len(defs)}')
for name, dp, ds, cp, cs in bad:
    print(f'SYSV MISMATCH {name}\n    def  {dp}\n         {ds}\n    decl {cp}\n         {cs}')
known = {n for n, _, _ in decl_blind if n in defs}
blind_rows = [(n, pl, sp) for n, pl, sp in decl_blind if n in defs]
for n, pl, sp in sorted(blind_rows):
    print(f'BLIND DECL {n} at {pl}: {sp}  (def {defs[n][1]})')
print(f'TOTAL mismatching declarations: {len(bad)}   value-or-handle spelling (benign): {len(benign)}   blind decls against a known def: {len(blind_rows)}   unresolved defs: {len(skipped)}')
if '-v' in sys.argv:
    for n, a, b in sorted(set(layout_conflicts)):
        print(f'LAYOUT CONFLICT {n}: {a}   vs   {b}')
if '-v' in sys.argv:
    for row in sorted(set(unmarked)): print('UNMARKED ' + row)
sys.exit(1 if bad else 0)
