"""Fetch pjsk card art into assets/card/ for the account "卡面" picker.

Why a tool and not a committed dump: one card is 2338x1440 RGBA (~4 MB as PNG),
and the JP 7.1.0 master has 1478 of them - 5.9 GB. So the repo ships a starter
set and this tool fills in whatever else the user wants.

    python .workbuddy/tools/fetch_card_art.py --per-character        # 1 per char
    python .workbuddy/tools/fetch_card_art.py --ids 3 149 400
    python .workbuddy/tools/fetch_card_art.py --characters 1 2 --rarity 4

Source layout (verified 2026-11-21):
    https://storage.sekai.best/sekai-jp-assets/character/member/<bundle>/card_normal.png
    https://storage.sekai.best/sekai-jp-assets/character/member/<bundle>/card_after_training.png
and the master table (assetbundleName -> character / rarity / attr) comes from
    https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main/cards.json

Output: <assets>/card/<bundle>[_after].png
    - downscaled to --width (default 800), because the largest consumer is the
      multi-result deck card at 255x363 in our 1920 canvas; 2338px would be 9x
      the pixels for no visible gain.
    - the master table's character/rarity/attr is copied into card-index.json so
      the picker can group and label without the 36 MB master file.
"""
import argparse
import json
import os
import sys
import time
import urllib.request

ASSET_BASE = 'https://storage.sekai.best/sekai-jp-assets'
CARDS_MASTER = 'https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main/cards.json'
CHARS_MASTER = 'https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main/gameCharacters.json'
# The asset mirror answers 403 to the default python-urllib UA (verified
# 2026-11-21), so every request carries a normal browser one.
HEADERS = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) CppSekai/fetch_card_art'}
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def fetch_bytes(url, timeout=60, attempts=4):
    """GET with retries. The mirror drops TLS handshakes now and then
    (`UNEXPECTED_EOF_WHILE_READING`) and one stalled read can otherwise sit on
    the whole run, so the timeout is short and a failure is retried."""
    last = None
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers=HEADERS)
            with OPENER.open(request, timeout=timeout) as response:
                return response.read()
        except Exception as exc:
            last = exc
            time.sleep(1.5 * (attempt + 1))
    raise last


def load_master(cache_path, url=CARDS_MASTER):
    if os.path.exists(cache_path):
        with open(cache_path, 'rb') as handle:
            return json.load(handle)
    raw = fetch_bytes(url, timeout=300)
    os.makedirs(os.path.dirname(cache_path) or '.', exist_ok=True)
    with open(cache_path, 'wb') as handle:
        handle.write(raw)
    return json.loads(raw)


def pick_cards(cards, args):
    by_id = {c['id']: c for c in cards}
    if args.ids:
        return [by_id[i] for i in args.ids if i in by_id]
    pool = cards
    if args.characters:
        wanted = set(args.characters)
        pool = [c for c in pool if c['characterId'] in wanted]
    if args.rarity:
        wanted = {'rarity_%d' % r for r in args.rarity}
        pool = [c for c in pool if c['cardRarityType'] in wanted]
    if not args.per_character:
        return sorted(pool, key=lambda c: c['id'])
    # Newest first per character, so the starter set is not all 1-star tutorial
    # cards. `releaseAt` is a millisecond stamp; missing -> 0.
    best = {}
    for card in pool:
        key = card['characterId']
        rank = (card.get('releaseAt') or 0, card['id'])
        if key not in best or rank > best[key][0]:
            best[key] = (rank, card)
    return [c for _rank, c in sorted(best.values(), key=lambda item: item[1]['id'])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ids', type=int, nargs='*')
    ap.add_argument('--characters', type=int, nargs='*')
    ap.add_argument('--rarity', type=int, nargs='*', default=[])
    ap.add_argument('--per-character', action='store_true', help='one card per character')
    ap.add_argument('--width', type=int, default=800)
    ap.add_argument('--assets', default=os.path.join(os.path.dirname(__file__), '..', '..', 'assets'))
    ap.add_argument('--trained', action='store_true', help='prefer the after-training art')
    ap.add_argument('--master-cache', default=os.path.join(os.path.dirname(__file__), 'cards.json'))
    ap.add_argument('--chars-cache', default=os.path.join(os.path.dirname(__file__), 'gameCharacters.json'))
    args = ap.parse_args()

    out_dir = os.path.abspath(os.path.join(args.assets, 'card'))
    os.makedirs(out_dir, exist_ok=True)
    cards = load_master(os.path.abspath(args.master_cache))
    chosen = pick_cards(cards, args)
    if not chosen:
        print('no cards matched', file=sys.stderr)
        return 1

    from PIL import Image  # only needed for the downscale
    import io

    # The game reads card-index.tsv, not the JSON: main.cpp has no json parser
    # and the picker only needs "bundle + label". Character names come from the
    # English fields of gameCharacters.json because every Japanese field in this
    # master dump is mojibake (the source text is not UTF-8).
    romaji = {}
    try:
        for entry in load_master(os.path.abspath(args.chars_cache), url=CHARS_MASTER):
            name = ('%s %s' % (entry.get('givenNameEnglish', ''),
                entry.get('firstNameEnglish', ''))).strip()
            if name:
                romaji[entry['id']] = name.title()
    except Exception:
        pass
    rows = {}
    index_path = os.path.join(out_dir, 'card-index.tsv')
    if os.path.exists(index_path):
        with open(index_path, 'r', encoding='utf-8') as handle:
            for line in handle:
                if line.startswith('#') or not line.strip():
                    continue
                parts = line.rstrip('\n').split('\t')
                if len(parts) >= 5:
                    rows[parts[0]] = parts

    for i, card in enumerate(chosen, 1):
        bundle = card['assetbundleName']
        trained = args.trained and card.get('initialSpecialTrainingStatus') in ('done', 'available')
        name = 'card_after_training.png' if trained else 'card_normal.png'
        url = '%s/character/member/%s/%s' % (ASSET_BASE, bundle, name)
        out_name = bundle + ('_after' if trained else '') + '.png'
        out_path = os.path.join(out_dir, out_name)
        print('[%d/%d] %s %s' % (i, len(chosen), bundle, name), end=' ... ', flush=True)
        if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
            print('have')
            rows[bundle] = [bundle, romaji.get(card['characterId'], '?'), card.get('attr', ''),
                card['cardRarityType'].replace('rarity_', ''), str(card['id'])]
            continue
        try:
            raw = fetch_bytes(url)
        except Exception as exc:  # a missing after-training art is normal
            print('skip (%s)' % exc)
            continue
        image = Image.open(io.BytesIO(raw)).convert('RGBA')
        if args.width and image.width > args.width:
            height = round(image.height * args.width / image.width)
            image = image.resize((args.width, height), Image.LANCZOS)
        # The cell only ever shows a portrait crop of this landscape sheet, so
        # downscale rather than resample on every draw.
        image.save(out_path, optimize=True)
        rows[bundle] = [bundle, romaji.get(card['characterId'], '?'), card.get('attr', ''),
            card['cardRarityType'].replace('rarity_', ''), str(card['id'])]
        print('%dx%d %d KB' % (image.width, image.height,
            os.path.getsize(out_path) // 1024))

    ordered = [rows[k] for k in sorted(rows, key=lambda k: int(rows[k][4] or 0))]
    with open(index_path, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('# bundle\tcharacter\tattr\trarity\tcardId\n')
        for row in ordered:
            handle.write('\t'.join(row) + '\n')
    print('wrote %d cards + card-index.tsv into %s' % (len(ordered), out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
