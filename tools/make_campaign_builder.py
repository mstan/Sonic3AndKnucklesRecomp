"""Freeze the existing owner-media converters and Pillow into one helper.

Build-host dependencies: PyInstaller and Pillow. The resulting game package
needs neither installed Python nor a separate setup application.
"""
import argparse
import ast
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    if sys.version_info<(3,11):raise RuntimeError('The converter build needs Python 3.11 or newer')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--work',required=True,type=Path)
    p.add_argument('--engine-root',type=Path,default=ROOT/'segagenesisrecomp')
    a=p.parse_args();out=a.out.resolve();work=a.work.resolve()
    out.mkdir(parents=True,exist_ok=True);work.mkdir(parents=True,exist_ok=True)
    sep=';' if sys.platform=='win32' else ':'
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--onefile','--console',
        '--name','S3KCampaignBuilder','--distpath',str(out),'--workpath',str(work/'work'),
        '--specpath',str(work),'--hidden-import','PIL.Image','--hidden-import','PIL.PngImagePlugin',
        '--exclude-module','tkinter','--exclude-module','numpy','--exclude-module','matplotlib']
    sources=[]
    for name in ('import_campaign.py','import_sound_donor.py','import_cd_sound_donor.py','build_stage_pack.py','split_campaign_packs.py',
                 'build_megamix_pack.py','import_sonic_cd.py','build_sonic_cd_pack.py'):
        sources.append(ROOT/'tools'/name)
        command+=['--add-data',str(ROOT/'tools'/name)+sep+'tools']
    for directory in ('trilogy','megamix'):
        for f in sorted((ROOT/'tools'/directory).rglob('*')):
            if f.is_file() and f.suffix.lower() in ('.py','.json','.txt','.png'):
                command+=['--add-data',str(f)+sep+str(Path('tools')/f.relative_to(ROOT/'tools').parent)]
                if f.suffix=='.py':sources.append(f)
    engine=a.engine_root.resolve()
    for name in ('disc_image.py','sgcd_owned_extract.py','owned_media.py'):
        sources.append(engine/'tools/segacd'/name)
        command+=['--add-data',str(engine/'tools/segacd'/name)+sep+'tools/segacd']
    # Converter modules are data so their existing relative data paths work.
    # Include their ordinary stdlib imports in the frozen interpreter too.
    hidden=set()
    for source in sources:
        for node in ast.walk(ast.parse(source.read_text(encoding='utf-8'))):
            modules=[a.name for a in node.names] if isinstance(node,ast.Import) else [node.module] if isinstance(node,ast.ImportFrom) and not node.level and node.module else []
            hidden.update(m for m in modules if m.split('.')[0] in sys.stdlib_module_names and m.split('.')[0]!='tkinter')
    for module in sorted(hidden):command+=['--hidden-import',module]
    command.append(str(ROOT/'tools/campaign_builder_entry.py'))
    subprocess.run(command,check=True)
    licenses=out/'campaign-builder-licenses';licenses.mkdir(exist_ok=True)
    python_licenses=[Path(sys.base_prefix)/'LICENSE.txt',Path(sys.base_prefix)/'LICENSE',
        Path(sys.base_prefix)/f'lib/python{sys.version_info.major}.{sys.version_info.minor}/LICENSE.txt',
        Path(f'/usr/share/doc/python{sys.version_info.major}.{sys.version_info.minor}/copyright')]
    python_license=next((f for f in python_licenses if f.is_file()),None)
    if python_license is None:raise RuntimeError('Build host Python license was not found')
    shutil.copy2(python_license,licenses/'Python.txt')
    versions={'Python':sys.version.split()[0]}
    for package in ('PyInstaller','Pillow'):
        dist=importlib.metadata.distribution(package);versions[package]=dist.version
        for f in dist.files or ():
            if f.name.lower().startswith(('license','copying')) and dist.locate_file(f).is_file():
                shutil.copy2(dist.locate_file(f),licenses/(package+'-'+f.name))
    (licenses/'versions.json').write_text(json.dumps(versions,sort_keys=True,indent=2)+'\n')
    print(out/('S3KCampaignBuilder.exe' if sys.platform=='win32' else 'S3KCampaignBuilder'))

if __name__=='__main__':main()
