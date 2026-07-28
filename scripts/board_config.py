"""
Board alias registry — maps friendly names to MAC addresses.
Managed by flash.py --register-alias and looked up by flash.py --board.
"""
import json, os
from pathlib import Path

CONFIG_FILE = Path(__file__).resolve().parent.parent / ".board_aliases.json"

def load():
    if CONFIG_FILE.exists():
        with open(CONFIG_FILE) as f:
            return json.load(f)
    return {}

def save(aliases):
    CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(CONFIG_FILE, "w") as f:
        json.dump(aliases, f, indent=2)
    print(f"  Saved {len(aliases)} alias(es) to {CONFIG_FILE}")

def resolve(alias):
    aliases = load()
    mac = aliases.get(alias)
    if not mac:
        print(f"  Unknown alias '{alias}'. Known aliases:")
        for a, m in aliases.items():
            print(f"    {a} -> {m}")
        return None
    return mac.upper()

def register(alias, mac):
    aliases = load()
    aliases[alias] = mac.upper()
    save(aliases)
    print(f"  Registered alias '{alias}' -> {mac.upper()}")

def list_aliases():
    aliases = load()
    if not aliases:
        print("  No board aliases registered.")
        print("  Use: python flash.py --register-alias <name>")
        return
    print("  Registered board aliases:")
    for alias, mac in sorted(aliases.items()):
        print(f"    {alias:20s} -> {mac}")
