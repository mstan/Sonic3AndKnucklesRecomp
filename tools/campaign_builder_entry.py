"""Headless entry for the shipped owner-media converter (no user Python)."""
from pathlib import Path
import runpy
import sys

if getattr(sys,'frozen',False):
    tools=Path(sys._MEIPASS)/'tools'
    sys.path.insert(0,str(tools))
    runpy.run_path(str(tools/'import_campaign.py'),run_name='__main__')
else:
    from import_campaign import main
    raise SystemExit(main())
