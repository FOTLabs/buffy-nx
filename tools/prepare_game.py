"""Extract a user-supplied Xbox ISO into game/; never download retail data."""
from pathlib import Path
import hashlib, json, sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"sdk"))
from tools.xiso.xdvdfs import Xiso
if len(sys.argv)!=2:raise SystemExit("Usage: python3 tools/prepare_game.py /path/to/your.iso")
dest=root/"game"
if dest.exists() and any(dest.iterdir()):raise SystemExit("game/ must be empty; refusing to overwrite")
with Xiso(sys.argv[1]) as image:
    entries=list(image.walk())
    item=next((e for d,e in entries if not d and e.name.lower()=="default.xbe"),None)
    if item is None:raise SystemExit("No root default.xbe")
    data=image._read_at(item.sector,item.size)
    expected=json.loads((root/"installer/game.json").read_text())["xbeSha256"]
    if hashlib.sha256(data).hexdigest()!=expected:raise SystemExit("Unsupported executable: this project targets the tested USA revision")
    for directory,e in entries:
        target=(dest/directory/e.name).resolve()
        if not target.is_relative_to(dest.resolve()):raise SystemExit("Invalid disc path")
        image.extract(e,str(target))
    # The generation pipeline uses this spelling on case-sensitive filesystems.
    actual=next(p for p in dest.iterdir() if p.name.lower()=="default.xbe")
    if actual.name!="Default.xbe":actual.rename(dest/"Default.xbe")
print("Extracted",len(entries),"files into game/")
