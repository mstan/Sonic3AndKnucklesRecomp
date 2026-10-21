"""Importer process fixture: a descendant retains stdout after its parent exits."""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
for name in ("disc", "s3k", "out", "receipt", "validator"):
    parser.add_argument(f"--{name}", required=True)
args = parser.parse_args()
subprocess.Popen([sys.executable, "-c", "import time; time.sleep(8)"],
                 stdin=subprocess.DEVNULL)
if args.disc == "failure.cue":
    print("S3K_CD_ERROR:fixture failure", flush=True)
    sys.exit(7)
Path(args.receipt).write_text("format=1\ncontent=fixture-content.pak\nmusic=fixture-music.pak\n")
print("S3K_CD_PROGRESS:1.0:fixture complete", end="", flush=True)
