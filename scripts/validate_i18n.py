#!/usr/bin/env python3
"""Valide la chaine de textes des modules. N'ecrit jamais de fichier (sauf --write-ratchet).

Le mode d'echec de la chaine est silencieux : un token `*_t` sans traduction
s'affiche brut a l'ecran, parce que generate_config_docs.py et
generate_runtimeui_manifest.py resolvent tous les deux par
`translations.get(token, token)`. Les JSON invalides sont avales par des
`except Exception: return {}`. Ce script est le seul garde-fou : il tourne en
`pre:` du build (section [esp32_base] de platformio.ini).

Severites :
  ERREUR      casse le build. Invariants structurels et couverture FR.
  AVERTISSEMENT  dette anglaise en cours de resorption, pilotee par le cliquet.

Usage :
    python scripts/validate_i18n.py [--strict] [--module NAME] [--json]
                                    [--max-lines N] [--ratchet FILE] [--write-ratchet]

Codes de sortie : 0 = OK, 1 = erreurs, 2 = echec interne du validateur.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

# PlatformIO fournit le repertoire projet via env (meme motif que
# generate_datamodel.py). Ne pas ajouter de `Import = type("Import", (), {})` :
# ce shadowing laisse env a None y compris sous PlatformIO.
env = None
try:
    Import("env")  # type: ignore[name-defined]  # noqa: F821
except Exception:
    env = None


ERROR = "ERROR"
WARN = "WARN"

KNOWN_LOCALES = ("fr", "en")
SOURCE_LOCALE = "fr"

# Prefixes de fichiers portant une locale dans leur nom.
LOCALIZED_STEMS = ("i18n", "cfgdocs", "cfgmods", "runtimeui")

# Mots identiques en francais et en anglais : une entree EN qui n'est composee
# que de ceux-la n'est pas une traduction manquante.
BILINGUAL_WORDS = {
    "mode", "modes", "format", "formats", "configuration", "configurations",
    "position", "positions", "station", "stations", "option", "options",
    "service", "services", "description", "descriptions", "information",
    "informations", "port", "ports", "version", "versions", "instance",
    "instances", "interface", "interfaces", "table", "tables", "image",
    "images", "page", "pages", "note", "notes", "date", "dates", "total",
    "normal", "local", "global", "contact", "contacts", "message", "messages",
    "action", "actions", "section", "sections", "test", "tests", "type",
    "types", "index", "source", "sources", "expert", "double", "simple",
    "auto", "max", "min", "code", "codes", "zone", "zones", "web", "cache",
    "cycle", "cycles", "digital", "public", "robot", "volume", "filtration",
    "point", "points", "orientation", "calibration", "correction", "direction",
    "distance", "duration", "extension", "fusion", "gain", "gains", "machine",
    "phase", "phases", "region", "relation", "session", "sessions", "solution",
    "tension", "transition", "union", "vision", "offset", "reset", "scan",
    "ping", "trace", "debug", "alarm", "alarms", "sensor", "sensors",
}

# Mots francais sans equivalent orthographique anglais. Liste volontairement
# courte : les faux amis (mode, format, port, service...) sont exclus, ils
# vivent dans BILINGUAL_WORDS.
FRENCH_MARKERS = {
    "le", "la", "les", "du", "des", "une", "dans", "pour", "avec", "est",
    "sont", "cette", "qui", "que", "aux", "ainsi", "alors", "apres", "avant",
    "avoir", "cet", "ces", "chaque", "depuis", "doit", "donc", "etre", "faire",
    "jusqu", "lorsque", "lorsqu", "mais", "meme", "nom", "permet", "peut",
    "puis", "selon", "seuil", "sous", "tous", "toutes", "valeur", "vers",
    "entre", "fors", "sinon", "aucun", "aucune", "plusieurs", "celui", "celle",
    "desactive", "affiche", "envoie", "recu", "duree", "sortie", "entree",
    "reseau", "bassin", "eau", "heure", "jour", "semaine", "mois", "annee",
    "niveau", "capteur", "sonde", "pompe", "vanne", "chauffage", "desinfection",
    "consigne", "fenetre", "ecran", "libelle", "mesure", "reglage", "defaut",

    # Ajouts issus de la campagne de traduction (2026-08-05). L'heuristique
    # d'origine ne voyait qu'environ deux tiers du franglais : sur IOModule,
    # 46 % des entrees reecrites n'etaient pas signalees, faute d'accent et de
    # mot-marqueur -- "Readable name de input analogique A16" passait.
    # Meme regle de selection : uniquement des mots sans homographe anglais.
    # Sont volontairement exclus : temperature, filtration, impulsion, canal,
    # resistance, debit, tension, active, activation, descendant, brut, fond,
    # applique -- tous homographes de l'anglais, ils feraient des faux positifs.
    "analogique", "digitale", "digitales", "numerique", "numero",
    "interne", "externe", "lisible", "utilise", "utilisee", "utilises",
    "utilisees", "appliquee", "appliques", "appliquees", "activee", "activees",
    "desactivee", "conservee", "conservees", "consideree", "considerees",
    "calculee", "cumulee", "publiee", "decalage", "decimales",
    "broche", "carte", "couche", "masque", "relais", "sorties", "entrees",
    "libelles", "valeurs", "periode", "frequence", "adresse", "couleur",
    "choisit", "choix", "demarrage", "redemarrage", "comptage", "compteur",
    "lecture", "lectures", "ecriture", "secondes", "moyenne", "vitesse",
    "etat", "etats", "pret", "prete", "fenetres", "horaire", "horaires",
    "physique", "logique", "autorise", "pont", "paire", "modele", "puce",
    "convertisseur", "moniteur", "puissance", "intensite", "energie",
    "supplementaires", "retroeclairage", "mouvement", "piscine", "remplissage",
    "alarme", "alarmes", "chlore", "volet", "bidon", "cuve", "humidite",

    # Deuxieme passe (2026-08-05) : mots releves sur les 28 entrees encore
    # franglaises APRES la campagne, que la version 2 ne voyait pas non plus
    # -- "Pressure Filtration Basse", "Mot de passe network", "Suivi setpoint
    # ORP", "Langue of affichage de interface". Le detecteur avait ete
    # renforce sans repasser sur les catalogues, d'ou l'angle mort.
    # Toujours la meme regle : aucun homographe anglais. Restent exclus pour
    # cette raison "gel" (silica gel, gel battery) et "impulsions" -- les
    # phrases concernees sont couvertes par un autre mot de la liste.
    "analogiques", "automatismes", "affichage", "bas", "basse", "dialoguer",
    "esclave", "fonctionnement", "haute", "hiver", "hors", "identifiant",
    "langue", "maintien", "mot", "pilotage", "pilote", "recopie", "repondre",
    "serveur", "suivi", "sur",
}

# Lettres latines accentuees uniquement : U+00D7 (multiplie) et U+00F7 (divise)
# sont dans la plage mais ne sont pas des lettres, et apparaissent legitimement
# dans les formules des textes anglais ("volume x cycles(T) / debit").
LATIN_LETTERS = u"\u00c0-\u00d6\u00d8-\u00f6\u00f8-\u00ff"
ACCENTED = re.compile(u"[" + LATIN_LETTERS + u"]")
ELISION = re.compile(u"\\b[dlnjmtscDLNJMTSC][\u2019']")
ALPHA_RUN = re.compile(u"[A-Za-z" + LATIN_LETTERS + u"]{3,}")


class Finding(object):
    __slots__ = ("severity", "code", "file", "module", "token", "detail")

    def __init__(self, severity, code, file, module=None, token=None, detail=""):
        self.severity = severity
        self.code = code
        self.file = file
        self.module = module
        self.token = token
        self.detail = detail

    def as_dict(self):
        return {
            "severity": self.severity,
            "code": self.code,
            "file": self.file,
            "module": self.module,
            "token": self.token,
            "detail": self.detail,
        }


def project_dir():
    # Sous PlatformIO le script est exec() par SCons : __file__ n'y est pas
    # defini, d'ou le passage par PROJECT_DIR puis le repli sur le cwd.
    if env is not None:
        try:
            value = env.get("PROJECT_DIR")
            if value:
                return Path(value)
        except Exception:
            pass
    try:
        return Path(__file__).resolve().parents[1]
    except NameError:
        return Path(os.getcwd())


def rel(root, path):
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def module_of(modules_root, path):
    """"Network/WifiModule" depuis .../src/Modules/Network/WifiModule/text/x.json"""
    try:
        parts = path.relative_to(modules_root).parts
    except ValueError:
        return None
    if "text" not in parts:
        return "/".join(parts[:-1]) or None
    return "/".join(parts[: parts.index("text")]) or None


def split_locale(name):
    """Reproduit la decoupe brute de generate_cfgdoc_chunks.py:145.

    On valide le comportement REEL de la chaine, pas le comportement souhaite :
    un fichier `i18n.fr.bak.json` y produit une locale `fr.bak`, qui creerait
    silencieusement un catalogue parasite dans data/wc/.
    """
    for stem in LOCALIZED_STEMS:
        # Forme non localisee (`runtimeui.json`) : a tester avant le prefixe,
        # sinon la decoupe brute en extrait une locale vide.
        if name == stem + ".json":
            return stem, None
        prefix = stem + "."
        if name.startswith(prefix) and name.endswith(".json"):
            return stem, name[len(prefix): -len(".json")]
    return None, None


def load_json_strict(root, path, findings):
    module = None
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        findings.append(Finding(ERROR, "FILE_UNREADABLE", rel(root, path), module, None, str(exc)))
        return None
    try:
        data = json.loads(raw)
    except ValueError as exc:
        line = getattr(exc, "lineno", "?")
        col = getattr(exc, "colno", "?")
        msg = getattr(exc, "msg", str(exc))
        findings.append(Finding(ERROR, "JSON_INVALID", rel(root, path), module, None,
                                "ligne %s colonne %s : %s" % (line, col, msg)))
        return None
    if not isinstance(data, dict):
        findings.append(Finding(ERROR, "PAYLOAD_NOT_OBJECT", rel(root, path), module, None,
                                "racine de type %s" % type(data).__name__))
        return None
    return data


def collect_tokens(node, out):
    """Tout `<cle>_t` de valeur str, a n'importe quelle profondeur.

    Meme regle que generate_runtimeui_manifest.py:313-326, plus generale que
    les label_t/help_t de generate_config_docs.py.
    """
    if isinstance(node, dict):
        for key, value in node.items():
            if isinstance(key, str) and key.endswith("_t") and isinstance(value, str):
                token = value.strip()
                if token:
                    out.add(token)
            else:
                collect_tokens(value, out)
    elif isinstance(node, list):
        for value in node:
            collect_tokens(value, out)


def language_words(text):
    """Mots porteurs de langue : suites alphabetiques de 3+ hors sigles.

    Les fragments d'identifiants snake_case sont ecartes : "aux_output",
    "object_id" ou "device_name" sont du code, pas de la langue. Sans ce
    filtre, le "aux" de "Micronova aux_output - GPIO relay" etait signale
    comme un mot francais.
    """
    words = []
    for match in ALPHA_RUN.finditer(text):
        before = text[match.start() - 1] if match.start() else ""
        after = text[match.end()] if match.end() < len(text) else ""
        if before == "_" or after == "_":
            continue
        word = match.group()
        if not word.isupper():
            words.append(word)
    return words


def is_language_bearing(text):
    for word in language_words(text):
        if len(word) >= 4 and word.lower() not in BILINGUAL_WORDS:
            return True
    return False


def french_residue(text):
    """('hard'|'soft'|None, detail) — indice de francais dans une valeur EN."""
    if ACCENTED.search(text):
        return "hard", "caractere accentue"
    if ELISION.search(text):
        return "hard", "elision francaise"
    for word in language_words(text):
        if word.lower() in FRENCH_MARKERS:
            return "soft", "mot francais '%s'" % word
    return None, ""


def scan(root, findings):
    """Retourne (catalogues, tokens_references, modules)."""
    modules_root = root / "src" / "Modules"
    if not modules_root.is_dir():
        raise RuntimeError("src/Modules introuvable sous %s" % root)

    catalogs = {}          # locale -> {token: (valeur, module)}
    referenced = {}        # token -> module du premier referent
    modules = set()

    for path in sorted(modules_root.rglob("text/*.json")):
        name = path.name
        module = module_of(modules_root, path)
        if module:
            modules.add(module)
        stem, locale = split_locale(name)
        relpath = rel(root, path)

        if stem is None:
            findings.append(Finding(WARN, "FILE_UNEXPECTED", relpath, module, None,
                                    "nom de fichier hors convention"))
            continue

        if locale is not None and locale not in KNOWN_LOCALES:
            findings.append(Finding(ERROR, "LOCALE_UNKNOWN", relpath, module, None,
                                    "locale '%s' inconnue (attendu : %s)"
                                    % (locale, ", ".join(KNOWN_LOCALES))))
            continue

        data = load_json_strict(root, path, findings)
        if data is None:
            continue

        if stem == "i18n":
            declared = data.get("locale")
            if isinstance(declared, str) and declared.strip() != locale:
                findings.append(Finding(WARN, "LOCALE_FIELD_MISMATCH", relpath, module, None,
                                        "champ locale='%s' vs nom de fichier '%s'"
                                        % (declared.strip(), locale)))
            translations = data.get("translations")
            if not isinstance(translations, dict):
                findings.append(Finding(ERROR, "TRANSLATIONS_NOT_OBJECT", relpath, module, None,
                                        "cle 'translations' absente ou non-objet"))
                continue

            keys = list(translations.keys())
            if keys != sorted(keys):
                findings.append(Finding(ERROR, "KEYS_UNSORTED", relpath, module, None,
                                        "cles non triees"))

            bucket = catalogs.setdefault(locale, {})
            for token, value in translations.items():
                if not isinstance(token, str) or not isinstance(value, str):
                    findings.append(Finding(ERROR, "ENTRY_NOT_STRING", relpath, module,
                                            str(token), "cle ou valeur non-str"))
                    continue
                if token in bucket:
                    previous, owner = bucket[token]
                    if previous != value:
                        findings.append(Finding(
                            ERROR, "TOKEN_COLLISION", relpath, module, token,
                            "%s='%s' vs %s='%s'" % (owner, previous[:40], module, value[:40])))
                    else:
                        findings.append(Finding(WARN, "TOKEN_DUPLICATE", relpath, module, token,
                                                "aussi defini par %s" % owner))
                    continue
                bucket[token] = (value, module)
        else:
            tokens = set()
            collect_tokens(data, tokens)
            for token in tokens:
                referenced.setdefault(token, module)

    return catalogs, referenced, modules


def evaluate(catalogs, referenced, findings):
    """Couverture, parite et qualite. Retourne la dette EN par module."""
    fr = catalogs.get(SOURCE_LOCALE, {})
    en = catalogs.get("en", {})
    debt = {}

    def bump(module):
        debt[module] = debt.get(module, 0) + 1

    for token, module in sorted(referenced.items()):
        if token not in fr:
            findings.append(Finding(ERROR, "TOKEN_MISSING_FR", "(catalogue fr)", module, token,
                                    "token reference sans traduction : s'affichera brut"))

    for token in sorted(set(fr) - set(referenced)):
        value, module = fr[token]
        findings.append(Finding(WARN, "FR_ORPHAN", "(catalogue fr)", module, token,
                                "cle jamais referencee"))

    for token in sorted(set(en) - set(fr)):
        value, module = en[token]
        findings.append(Finding(WARN, "EN_ORPHAN", "(catalogue en)", module, token,
                                "cle EN sans pendant FR"))

    for token in sorted(fr):
        fr_value, module = fr[token]
        if token not in en:
            findings.append(Finding(WARN, "EN_MISSING", "(catalogue en)", module, token,
                                    "sans traduction anglaise : s'affichera brut en EN"))
            bump(module)
            continue
        en_value = en[token][0]
        if en_value.strip() == fr_value.strip():
            if is_language_bearing(fr_value):
                findings.append(Finding(WARN, "EN_UNTRANSLATED", "(catalogue en)", module, token,
                                        "identique au FR : '%s'" % fr_value[:50]))
                bump(module)
            continue
        kind, detail = french_residue(en_value)
        if kind is not None:
            findings.append(Finding(WARN, "EN_FRENCH_RESIDUE", "(catalogue en)", module, token,
                                    "%s (%s) : '%s'" % (detail, kind, en_value[:50])))
            bump(module)

    return debt


def check_web_catalog(root, findings):
    """Garde anti-regression sur le catalogue du shell web (sain, ecrit a la main)."""
    base = root / "data" / "webinterface" / "i18n"
    if not base.is_dir():
        return
    loaded = {}
    for locale in KNOWN_LOCALES:
        path = base / ("%s.json" % locale)
        if not path.is_file():
            findings.append(Finding(WARN, "WEB_CATALOG_PARITY", rel(root, path), None, None,
                                    "catalogue web absent"))
            continue
        data = load_json_strict(root, path, findings)
        if data is None:
            continue
        loaded[locale] = data.get("translations", data)

    fr = loaded.get("fr") or {}
    en = loaded.get("en") or {}
    if not fr or not en:
        return
    relpath = rel(root, base / "en.json")
    for token in sorted(set(fr) - set(en)):
        findings.append(Finding(WARN, "WEB_CATALOG_PARITY", relpath, None, token, "absent du web EN"))
    for token in sorted(set(en) - set(fr)):
        findings.append(Finding(WARN, "WEB_CATALOG_PARITY", relpath, None, token, "absent du web FR"))
    for token in sorted(set(fr) & set(en)):
        value = en[token]
        if not isinstance(value, str):
            continue
        kind, detail = french_residue(value)
        if kind == "hard":
            findings.append(Finding(WARN, "WEB_CATALOG_PARITY", relpath, None, token,
                                    "residu francais : %s" % detail))


def check_synthetic_ports(findings):
    """Les libelles de ports doivent exister dans toutes les locales connues.

    Ces tokens ne vivent dans aucun catalogue de module : ils sont rendus par
    gabarit et injectes dans data/wc/ au build. Sans ce controle, un gabarit
    oubliant une locale passerait inapercu jusqu'a l'affichage.
    """
    try:
        sys.path.insert(0, str(project_dir() / "scripts"))
        import io_port_labels
    except Exception as exc:
        findings.append(Finding(ERROR, "SYNTHETIC_MODULE_BROKEN", "scripts/io_port_labels.py",
                                None, None, str(exc)))
        return

    # Completude des tables de langue. C'est l'invariant utile : render() retombe
    # sur la cle brute quand une locale manque, donc le token existe quand meme et
    # une comparaison d'ensembles de tokens ne verrait rien.
    tables = [("DEVICES", io_port_labels.DEVICES), ("PATTERNS", io_port_labels.PATTERNS)]
    for table_name, table in tables:
        for key, per_locale in sorted(table.items()):
            for locale in KNOWN_LOCALES:
                if not per_locale.get(locale):
                    findings.append(Finding(ERROR, "SYNTHETIC_LOCALE_MISSING",
                                            "scripts/io_port_labels.py", None,
                                            "%s.%s" % (table_name, key),
                                            "locale %s absente" % locale))
    for locale in KNOWN_LOCALES:
        if not io_port_labels.NONE_LABEL.get(locale):
            findings.append(Finding(ERROR, "SYNTHETIC_LOCALE_MISSING",
                                    "scripts/io_port_labels.py", None, "NONE_LABEL",
                                    "locale %s absente" % locale))

    # Chaque port doit se rendre sans erreur de gabarit dans toutes les locales.
    for profile in sorted(io_port_labels.PROFILE_PORTS):
        try:
            rendered = io_port_labels.synthetic_translations(profile)
        except Exception as exc:
            findings.append(Finding(ERROR, "SYNTHETIC_RENDER_FAILED", "scripts/io_port_labels.py",
                                    None, None, "profil %s : %s" % (profile, exc)))
            continue
        reference = set(rendered.get(SOURCE_LOCALE, {}))
        for locale in KNOWN_LOCALES:
            for token in sorted(reference - set(rendered.get(locale, {}))):
                findings.append(Finding(ERROR, "SYNTHETIC_TOKEN_MISSING",
                                        "scripts/io_port_labels.py", None, token,
                                        "profil %s, locale %s" % (profile, locale)))


# 2 : FRENCH_MARKERS elargi et fragments snake_case ecartes (2026-08-05).
# 3 : deuxieme passe de FRENCH_MARKERS apres correction des 28 residus que la
#     version 2 laissait encore passer (2026-08-05).
# Un changement de valeur invalide le cliquet existant jusqu'a sa regeneration
# par --write-ratchet, sans quoi les plafonds compareraient deux mesures
# differentes.
RATCHET_HEURISTIC_VERSION = 3


def load_ratchet(path, findings):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except Exception as exc:
        print("[validate_i18n] cliquet illisible : %s" % exc)
        return None
    if data.get("heuristic_version") != RATCHET_HEURISTIC_VERSION:
        findings.append(Finding(WARN, "RATCHET_STALE", path, None, None,
                                "heuristic_version=%s attendu %d : cliquet ignore"
                                % (data.get("heuristic_version"), RATCHET_HEURISTIC_VERSION)))
        return None
    caps = data.get("modules")
    return caps if isinstance(caps, dict) else {}


def apply_ratchet(caps, debt, findings):
    for module in sorted(debt):
        cap = caps.get(module)
        if cap is None:
            findings.append(Finding(ERROR, "RATCHET_UNKNOWN_MODULE", "(cliquet)", module, None,
                                    "dette=%d sans plafond declare" % debt[module]))
        elif debt[module] > cap:
            findings.append(Finding(ERROR, "RATCHET_EXCEEDED", "(cliquet)", module, None,
                                    "dette=%d > plafond=%d" % (debt[module], cap)))


def write_ratchet(path, debt):
    payload = {
        "_comment": "Plafond de dette anglaise par module. Ne doit que decroitre. "
                    "Genere par: python scripts/validate_i18n.py --write-ratchet",
        "heuristic_version": RATCHET_HEURISTIC_VERSION,
        "modules": {k: debt[k] for k in sorted(debt)},
    }
    Path(path).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("[validate_i18n] cliquet ecrit : %s (%d modules)" % (path, len(debt)))


def report(findings, debt, max_lines):
    by_code = {}
    for finding in findings:
        by_code.setdefault((finding.severity, finding.code), []).append(finding)

    for severity in (ERROR, WARN):
        for (sev, code), items in sorted(by_code.items()):
            if sev != severity:
                continue
            print("%-5s %-22s %d" % (sev, code, len(items)))
            for finding in items[:max_lines]:
                where = finding.token or finding.file
                detail = (" — " + finding.detail) if finding.detail else ""
                print("        %s%s" % (where, detail))
            if len(items) > max_lines:
                print("        … (+%d autres)" % (len(items) - max_lines))

    if debt:
        print("")
        print("%-34s %s" % ("module", "dette EN"))
        for module in sorted(debt, key=lambda m: (-debt[m], m)):
            print("%-34s %6d" % (module, debt[module]))


def main(argv=None):
    parser = argparse.ArgumentParser(prog="validate_i18n.py",
                                     description="Valide les catalogues i18n des modules.")
    parser.add_argument("--strict", action="store_true",
                        help="les avertissements deviennent des erreurs")
    parser.add_argument("--module", default=None,
                        help="restreint le rapport a un module (ex. Network/WifiModule)")
    parser.add_argument("--json", action="store_true", help="sortie machine")
    parser.add_argument("--max-lines", type=int, default=20,
                        help="lignes de detail par code (defaut 20)")
    parser.add_argument("--ratchet", default=None, help="fichier de plafonds de dette EN")
    parser.add_argument("--write-ratchet", default=None, nargs="?", const="scripts/i18n_ratchet.json",
                        help="ecrit le cliquet depuis la dette mesuree")
    args = parser.parse_args(argv)

    root = project_dir()
    findings = []
    try:
        catalogs, referenced, modules = scan(root, findings)
    except Exception as exc:
        print("[validate_i18n] echec interne : %s" % exc)
        return 2

    debt = evaluate(catalogs, referenced, findings)
    check_web_catalog(root, findings)
    check_synthetic_ports(findings)

    if args.write_ratchet:
        write_ratchet(args.write_ratchet, debt)
        return 0

    if args.ratchet:
        caps = load_ratchet(args.ratchet, findings)
        if caps is not None:
            apply_ratchet(caps, debt, findings)

    shown = findings
    shown_debt = debt
    if args.module:
        shown = [f for f in findings if f.module == args.module]
        shown_debt = {k: v for k, v in debt.items() if k == args.module}

    errors = [f for f in findings if f.severity == ERROR]
    warnings = [f for f in findings if f.severity == WARN]

    if args.json:
        print(json.dumps({
            "errors": len(errors),
            "warnings": len(warnings),
            "debt": debt,
            "findings": [f.as_dict() for f in shown],
        }, ensure_ascii=False, indent=2))
    else:
        fr_total = len(catalogs.get(SOURCE_LOCALE, {}))
        print("[validate_i18n] %d modules, %d tokens references, %d cles FR, %d cles EN"
              % (len(modules), len(referenced), fr_total, len(catalogs.get("en", {}))))
        report(shown, shown_debt, args.max_lines)
        total_debt = sum(debt.values())
        ratio = (100.0 * total_debt / fr_total) if fr_total else 0.0
        print("")
        print("[validate_i18n] erreurs=%d avertissements=%d  dette_en=%d/%d (%.1f%%)"
              % (len(errors), len(warnings), total_debt, fr_total, ratio))

    if errors:
        return 1
    if args.strict and warnings:
        return 1
    return 0


# Sous PlatformIO le script est execute par SCons : sys.argv porte les arguments
# de SCons, pas les notres. On force donc les valeurs par defaut dans ce contexte,
# sinon argparse les rejette et fait echouer le build avec un message trompeur.
_under_scons = (env is not None) or ("SCons.Script" in sys.modules)
_exit_code = main([] if _under_scons else None)
if _exit_code:
    # Une SystemExit non nulle arrete le build comme l'execution nue.
    raise SystemExit(_exit_code)
