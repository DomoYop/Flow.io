#!/usr/bin/env python3
"""Resout les conflits git sur les catalogues i18n, par cle et non par ligne.

Outil manuel, hors build : a lancer depuis la racine du depot pendant un merge
en conflit, avant de relire le resultat et de faire `git add`. N'ecrit que les
fichiers qu'il sait fusionner sans arbitrage.

Pourquoi il existe : les i18n.<locale>.json sont des dictionnaires plats. Git
conflicte sur des lignes voisines alors que la question se pose cle par cle, et
les resolutions manuelles y sont piegeuses -- une branche qui ajoute ses
traductions en fin de fichier entre en conflit avec une branche qui a trie le
catalogue, et le tri comme l'absence de doublon sont exiges par validate_i18n.py.
Le piege classique est `git checkout --theirs -- <fichier>` : il prend le fichier
entier de l'autre cote, pas seulement les hunks en conflit, donc il efface les
modifications faites ailleurs dans le meme fichier.

Le script relit les trois versions conservees par git dans l'index (:1 base,
:2 ours, :3 theirs) et applique la regle 3-way sur chaque cle : si un seul cote
a bouge il gagne, si les deux ont fait le meme changement il n'y a pas de
conflit, et seul un desaccord reel est remonte a l'humain -- auquel cas le
fichier n'est pas ecrit et le script sort en erreur.

Usage :
    python scripts/resolve_i18n_conflicts.py            # tous les i18n en conflit
    python scripts/resolve_i18n_conflicts.py a.json b.json
"""

import json
import subprocess
import sys

# Sentinelle : distingue "cle absente" de "cle presente valant None".
MISSING = object()


def git(*args):
    proc = subprocess.run(["git", *args], capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace").strip())
    return proc.stdout.decode("utf-8")


def conflicted_i18n_files():
    """Les catalogues i18n actuellement en conflit, chemins relatifs a la racine."""
    out = git("diff", "--name-only", "--diff-filter=U")
    return [
        path for path in out.splitlines()
        if path.rsplit("/", 1)[-1].startswith("i18n.") and path.endswith(".json")
    ]


def stage(path, number):
    """Version du fichier au stage donne : 1 base, 2 ours, 3 theirs."""
    return json.loads(git("show", f":{number}:{path}"))


def merge_catalogs(base, ours, theirs):
    """Fusion 3-way d'un dict plat. Renvoie (resultat, cles en conflit reel)."""
    merged = {}
    conflicts = []
    for key in sorted(set(base) | set(ours) | set(theirs)):
        b = base.get(key, MISSING)
        o = ours.get(key, MISSING)
        t = theirs.get(key, MISSING)
        if o == t:
            keep = o          # les deux cotes disent la meme chose
        elif o == b:
            keep = t          # seul theirs a modifie ou supprime
        elif t == b:
            keep = o          # seul ours a modifie ou supprime
        else:
            conflicts.append(key)
            continue
        if keep is not MISSING:
            merged[key] = keep
    return merged, conflicts


def resolve(path):
    """Fusionne un catalogue. Renvoie True si le fichier a ete ecrit."""
    try:
        base, ours, theirs = (stage(path, n) for n in (1, 2, 3))
    except RuntimeError as exc:
        print(f"{path} : stages introuvables ({exc})", file=sys.stderr)
        print("  ce fichier n'est pas en conflit, ou aucun merge n'est en cours.",
              file=sys.stderr)
        return False

    for name, doc in (("base", base), ("ours", ours), ("theirs", theirs)):
        if "translations" not in doc:
            print(f"{path} : version {name} sans cle 'translations', abandon",
                  file=sys.stderr)
            return False

    merged, conflicts = merge_catalogs(
        base["translations"], ours["translations"], theirs["translations"]
    )

    if conflicts:
        print(f"{path} : {len(conflicts)} cle(s) a arbitrer a la main, "
              f"fichier laisse en l'etat", file=sys.stderr)
        for key in conflicts:
            print(f"  {key}", file=sys.stderr)
            print(f"    ours   : {ours['translations'][key]!r}", file=sys.stderr)
            print(f"    theirs : {theirs['translations'][key]!r}", file=sys.stderr)
        return False

    # Les autres cles racine (locale, absente de certains catalogues) sont
    # reprises telles quelles : seul le catalogue est fusionne.
    doc = dict(ours)
    doc["translations"] = merged
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False, sort_keys=True)
        fh.write("\n")

    added = sorted(set(merged) - set(ours["translations"]))
    removed = sorted(set(ours["translations"]) - set(merged))
    print(f"{path} : {len(merged)} cles (+{len(added)} / -{len(removed)})")
    for key in removed:
        print(f"  - {key}")
    return True


def main(argv):
    paths = argv[1:] or conflicted_i18n_files()
    if not paths:
        print("Aucun catalogue i18n en conflit.")
        return 0

    ok = all([resolve(path) for path in paths])
    if ok:
        print("\nRelire le diff, puis : git add " + " ".join(paths))
        print("Verifier ensuite avec : "
              "python scripts/validate_i18n.py --ratchet scripts/i18n_ratchet.json")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
