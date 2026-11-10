"""Compare bundled experimental codes with their original manual AR payload.

Requires caller-owned ROM/state/settings and a pinned .bin from the collection.
All runs use separate portable copies. No input assets or normal config change.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess


def setkey(text, section, key, value):
    value = json.dumps(value)
    match = re.search(r'(?ms)(^\[' + re.escape(section) + r'\]\s*\n)(.*?)(?=^\[|\Z)', text)
    if not match:
        return text + f'\n[{section}]\n{key} = {value}\n'
    content, count = re.subn(r'(?m)^' + re.escape(key) + r'\s*=.*$', f'{key} = {value}', match[2])
    if not count:
        content += f'{key} = {value}\n'
    return text[:match.start(2)] + content + text[match.end(2):]


def run_matrix(args):
    source = json.loads(args.case.read_text(encoding='utf-8-sig'))
    rom = Path(source['rom'])
    state = Path(source['savestate']) if source.get('savestate') else None
    originals = [x for x in (rom, state, rom.with_suffix('.sav')) if x and x.exists()]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
    patch = args.patch.read_bytes()
    if args.visibility:
        pairs = re.findall(r'(?m)^([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8})\s*$',patch.decode())
        words = [int(word,16) for pair in pairs for word in pair]
        assert words, 'Provide the reviewed culling-only .mch reference'
    else:
        words = list(struct.unpack('<' + 'I' * (len(patch)//4), patch))
    if not args.visibility and args.ratio != (16,10):
        assert len(words) % 6 == 0
        for i in range(0,len(words),6):
            assert words[i] >> 28 == 9 and words[i+1] == 0x1555
            assert words[i+2] >> 28 == 1 and words[i+3:i+6] == [0x199a,0xd2000000,0]
            words[i+3] = (4096*args.ratio[0] + args.ratio[1]//2)//args.ratio[1]
    manual = 'CAT 0 Verification\nCODE 1 Original collection payload\n' + ''.join(
        f'{words[i]:08X} {words[i+1]:08X}\n' for i in range(0,len(words),2))
    args.output.mkdir(parents=True,exist_ok=False)
    root = args.output.resolve()
    cfg = args.settings.read_text(encoding='utf-8-sig')
    renderer_fov = getattr(args,'renderer_fov',None)
    view = args.view or ((342 if args.ratio==(16,9) else 682),192)
    assert len(view)==2 and 256<=view[0]<=768 and 192<=view[1]<=768
    frames = [x['frame'] for x in source['captures']['frames']] if args.source_replay else [1,60,120]

    def run(task):
        name,renderer = task
        directory=root/f'{renderer}-{name}'; runtime=directory/'runtime'; (runtime/'portable').mkdir(parents=True)
        executable = args.before if name == 'renderer-before' else args.exe
        for original,destination in ((executable,runtime/'melonDS.exe'),(rom,runtime/'game.nds'),
                                     (state,runtime/'game.state'),(rom.with_suffix('.sav'),runtime/'game.sav')):
            if original and original.exists(): shutil.copy2(original,destination)
        text=cfg
        values={'3D':{'Renderer':renderer},'Audio':{'Volume':0},'Screen':{'VSync':False},
            'Instance0':{'EnableCheats':name=='manual','CheatFilePath':'','SaveFilePath':'','SavestatePath':''},
            'JIT':{'Enable':not args.interpreter},'3D.GL':{
                'ScaleFactor':args.scale,'WidescreenMethod':1 if args.visibility or name.startswith('renderer-') else 2 if name=='patch' else 0,
                'WidescreenWidth':view[0] if args.visibility else 342 if name.startswith('renderer-') else 256,'WidescreenHeight':view[1] if args.visibility else 192,
                'WidescreenCustom':args.visibility and renderer_fov is not None,
                'WidescreenAspectWidth':args.ratio[0],'WidescreenAspectHeight':args.ratio[1],
                'WidescreenFOVPercent':renderer_fov or 100,'WidescreenDisplay':0,'WidescreenGameFixes':args.visibility and name=='patch',
                'WidescreenPatchAspectWidth':args.ratio[0],
                'WidescreenPatchAspectHeight':args.ratio[1],'WidescreenExpandNarrowBorders':False,'MSAA':False}}
        for section,keys in values.items():
            for key,value in keys.items(): text=setkey(text,section,key,value)
        (runtime/'portable/melonDS.toml').write_text(text,encoding='utf-8')
        (directory/'settings.toml').write_text(text,encoding='utf-8')
        if name=='manual': (runtime/'game.mch').write_text(manual,encoding='utf-8')
        case=dict(source);case.update(rom=str(runtime/'game.nds'),outputDir=str(directory/'capture'),warmupFrames=0,
            steps=source['steps'] if args.source_replay else [{'release':True,'releaseKeys':True,'frames':122}],
            captures={'wholeSceneCsv':True,'frames':[{'frame':n,'surfaces':['finalTop','finalBottom']} for n in frames]})
        if state:case['savestate']=str(runtime/'game.state')
        path=directory/'case.json';path.write_text(json.dumps(case,indent=2),encoding='utf-8')
        env=dict(os.environ);env['PATH']=str(args.exe.parent)+';C:/msys64/ucrt64/bin;'+env['PATH']
        env['QT_PLUGIN_PATH']=str(args.exe.parent);env['SDL_AUDIODRIVER']='dummy'
        for key in list(env):
            if key.startswith(('WIDEMELON_','CULL_','SOL_')):env.pop(key)
        with (directory/'run.log').open('wb') as log:
            result=subprocess.run([str(runtime/'melonDS.exe'),'--renderer-test',str(path)],cwd=runtime,env=env,
                stdout=log,stderr=subprocess.STDOUT,timeout=240,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        verdict=json.loads((directory/'capture/verdict.json').read_text())
        record=dict(name=name,renderer=renderer,exit=result.returncode,completed=verdict.get('completed',False))
        print(json.dumps(record),flush=True)
        assert record['exit']==0 and record['completed'],record
        return record

    names=['native','manual','patch'] + (['renderer-before','renderer-after'] if args.before else [])
    tasks=[(name,renderer) for renderer in args.renderers for name in names]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: runs=list(pool.map(run,tasks))
    checks=[]
    for renderer in args.renderers:
        for first,second in [('manual','patch')] + ([('renderer-before','renderer-after')] if args.before else []):
            for frame in frames:
                for screen in ('top','bottom'):
                    file=f'capture/exact-frame-captures/frame{frame:06d}/final-{screen}.png'
                    a=(root/f'{renderer}-{first}'/file).read_bytes();b=(root/f'{renderer}-{second}'/file).read_bytes()
                    checks.append(dict(renderer=renderer,first=first,second=second,frame=frame,screen=screen,identical=a==b))
    unchanged=all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    summary=dict(runs=runs,checks=checks,originalsUnchanged=unchanged,
        executableSHA256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),ratio=args.ratio,patch=args.patch.name)
    (root/'results.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    assert unchanged and all(x['identical'] for x in checks), 'Comparison mismatch; inspect results.json'
    print(f'{len(checks)} identical image comparisons; original assets unchanged.',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('case','settings','patch','exe','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--before',type=Path)
    parser.add_argument('--ratio',type=lambda x:tuple(map(int,x.split(':'))),default=(16,10))
    parser.add_argument('--renderers',type=int,nargs='+',default=[1,2])
    parser.add_argument('--scale',type=int,default=2)
    parser.add_argument('--interpreter',action='store_true')
    parser.add_argument('--visibility',action='store_true',help='Compare a renderer visibility profile with a culling-only .mch reference')
    parser.add_argument('--view',type=lambda x:tuple(map(int,x.split(':'))),help='Renderer view width:height for visibility checks')
    parser.add_argument('--source-replay',action='store_true',help='Preserve the source inputs and capture frames')
    parser.add_argument('--renderer-fov',type=int,help='Use Custom renderer settings with --ratio and this FOV percentage')
    args=parser.parse_args()
    for key in ('case','settings','patch','exe','output','before'):
        value=getattr(args,key)
        if value:setattr(args,key,value.resolve())
    run_matrix(args)
