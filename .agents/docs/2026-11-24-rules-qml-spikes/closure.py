import re, sys, json, subprocess, os
Q = os.path.expanduser("~/.xlings/data/xpkgs/xim-x-qt/6.11.1")
IMP = re.compile(r'^\s*import\s+([A-Za-z_][\w.]*)(?:\s+[\d.]+)?(?:\s+as\s+\w+)?\s*;?\s*$', re.M)
def strip(text):
    out=[];i=0
    while i<len(text):
        if text.startswith('//',i):
            while i<len(text) and text[i]!='\n': i+=1
        elif text.startswith('/*',i):
            j=text.find('*/',i+2); i=len(text) if j<0 else j+2; out.append(' ')
        elif text[i]=='"':
            j=i+1
            while j<len(text) and text[j]!='"': j+= 2 if text[j]=='\\' else 1
            out.append('""'); i=j+1
        else: out.append(text[i]); i+=1
    return ''.join(out)
def file_imports(path):
    return IMP.findall(strip(open(path,encoding='utf-8').read()))
def qmldir_follow(uri, own):
    rel = uri.replace('.','/')
    d = os.path.join(Q,'qml',rel)
    f = os.path.join(d,'qmldir')
    if not os.path.isfile(f): return None, []
    deps=[]
    for line in open(f,encoding='utf-8'):
        t=line.split()
        if not t: continue
        if t[0] in ('optional','default') and len(t)>=3 and t[1]=='import': deps.append(t[2]); continue
        if t[0]=='import' and len(t)>=2: deps.append(t[1]); continue
        if t[0]=='depends' and len(t)>=2: deps.append(t[1])
    # the module's own .qml files import further modules (the scanner reads them)
    for dp,dns,fns in os.walk(d):
        dns[:]=sorted(x for x in dns if not os.path.isfile(os.path.join(dp,x,'qmldir')))   # a nested module is its own module
        for fn in sorted(fns):
            if fn.endswith('.qml'):
                deps.extend(file_imports(os.path.join(dp,fn)))
    return rel, deps
def closure(roots, own=()):
    seen={}; todo=list(roots)
    while todo:
        u=todo.pop()
        if u in seen or u in own: continue
        rel,deps=qmldir_follow(u,own)
        if rel is None: seen[u]=None; continue
        seen[u]=rel; todo.extend(deps)
    return seen
def scanner(qmldir_root):
    j=subprocess.run([Q+'/libexec/qmlimportscanner','-rootPath',qmldir_root,'-importPath',Q+'/qml'],capture_output=True,text=True,env={'LD_LIBRARY_PATH':Q+'/lib','LC_ALL':'C.UTF-8'}).stdout
    return {e['name']:e.get('relativePath') for e in json.loads(j) if e.get('type')=='module'}
if __name__=='__main__':
    root=sys.argv[1]
    roots=set()
    for dp,_,fs in os.walk(root):
        for f in fs:
            if f.endswith('.qml'): roots.update(file_imports(os.path.join(dp,f)))
    mine=closure(roots)
    sc=scanner(root)
    mine_set={k for k,v in mine.items() if v is not None}
    print("imports:",sorted(roots))
    print("mine=%d scanner=%d"%(len(mine_set),len(sc)))
    print("scanner-only:",sorted(set(sc)-mine_set))
    print("mine-only   :",sorted(mine_set-set(sc)))
