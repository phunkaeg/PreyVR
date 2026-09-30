"""Map native status paths from an extracted HUD and JPEXS offline exports.

Produces metadata only. No game assets or decompiled scripts belong in Git.
JPEXS commands and asset identity are documented in the native-wrist note.
"""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from extract_native_hud import require, sha256_file

MOVIE_SHA256 = "35974883f152e345834ca690c8f6cb280152cee0ae724280e3cc98f75fb9c9dc"
DEFINITION_SHA256 = "57b89b80082a6dc93ae4b947739acf0cfb2c7c6e07edac137daac584925eb1df"


def placements(tags):
    """Initial named placements only; not a simulation of ActionScript/animation."""
    result = {}
    for tag in tags:
        if tag.get("type") == "ShowFrameTag":
            break
        if tag.get("type", "").startswith("PlaceObject") and tag.get("name"):
            name = tag.get("name")
            require(name not in result and tag.get("characterId") is not None,
                    "ambiguous initial named placement")
            matrix = tag.find("matrix")
            result[name] = dict(sprite_id=int(tag.get("characterId")), depth=int(tag.get("depth")),
                                matrix={} if matrix is None else dict(matrix.attrib))
    return result


def map_hud(movie, binary_definition, movie_xml, scripts):
    from extract_native_hud import decode_xml
    require(sha256_file(movie) == MOVIE_SHA256, "unsupported patched HUD movie")
    require(sha256_file(binary_definition) == DEFINITION_SHA256, "unsupported patched HUD definition")
    definition = decode_xml(Path(binary_definition).read_bytes())
    tree = ET.parse(movie_xml).getroot()
    require(tree.get("gfx") == "true" and tree.get("version") == "8", "expected GFx8 export")
    sprites = {int(e.get("spriteId")): e for e in tree.iter("item") if e.get("type") == "DefineSpriteTag"}

    def resolve(path):
        pieces = path.split(".")
        require(pieces[0] == "_root", "expected absolute movie path")
        tags = tree.findall("./tags/item")
        trace = []
        for piece in pieces[1:]:
            choices = placements(tags)
            require(piece in choices, f"missing named movie object: {path}")
            node = choices[piece]
            trace.append(dict(name=piece, **node))
            sprite = sprites.get(node["sprite_id"])
            require(sprite is not None, "named object is not a sprite")
            tags = sprite.findall("./subTags/item")
        return trace, placements(tags)

    root = Path(scripts) / "scripts/__Packages/code/HUD"
    widgets = {}
    for name, script, variable in (("health", "Health.as", "healthLoc"),
                                    ("psi", "PsiMeter.as", "Loc"),
                                    ("suit", "Armor.as", "Loc")):
        source = (root / script).read_text(encoding="utf-8-sig")
        match = re.search(r"static var " + variable + r" = (_root\.[\w.]+);", source)
        require(match is not None, f"missing {name} display-object reference")
        path = match.group(1)
        trace, children = resolve(path)
        widgets[name] = dict(path=path, placement=trace[-1], ancestor_placements=trace[:-1],
                             children=children, script=script, script_sha256=sha256_file(root / script))
    parent = "_root.safe.quadrant_SW"
    _, siblings = resolve(parent)
    required = {"bg_mc", "bg2_mc", "line_mc", "health_mc", "psi_mc", "armor_mc", "status_effects"}
    require(required <= set(siblings), "status-group membership changed")
    functions = {}
    for function in definition.iter("function"):
        name = function.get("name", "")
        if name.startswith(("health", "armor", "psi", "bleed")) or name in ("setWidgetsScale", "setSafeFrame"):
            functions[name] = dict(target=function.get("funcname"), parameters=[dict(p.attrib) for p in function])
    return dict(environment="offline", movie_sha256=MOVIE_SHA256, definition_sha256=DEFINITION_SHA256,
                exporter=tree.get("_generator"), movie_xml_sha256=sha256_file(movie_xml), widgets=widgets,
                common_parent=parent, siblings=siblings,
                proposed_capture_children=sorted(required, key=lambda name: siblings[name]["depth"]),
                exclude_siblings=sorted(set(siblings) - required), functions=functions,
                limits="Initial authored placements and decoded script references; not live transforms, widget visibility, final bounds, or a render-isolation implementation.")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("movie", "definition", "movie-xml", "scripts", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    args = p.parse_args()
    try:
        report = map_hud(args.movie, args.definition, args.movie_xml, args.scripts)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({name: value["path"] for name, value in report["widgets"].items()}, indent=2))
    except (ValueError, OSError, ET.ParseError) as error:
        p.exit(1, f"Mapping refused: {error}\n")
