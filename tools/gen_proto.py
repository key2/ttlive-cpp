#!/usr/bin/env python3
"""
Generate .proto (proto3) files from the betterproto2-generated TikTokLiveProto
Python package.

betterproto2 keeps full field metadata (number, proto_type, repeated, map,
optional, group/oneof) on each dataclass field, and enum classes are plain
IntEnum subclasses. We walk the whole ``TikTokLiveProto.v3`` package, import
every module so all classes register, then emit one big proto file per
Python module namespace.

Because resolving the exact original proto package layout / import graph is
brittle, we take the pragmatic route used by many reverse-engineered stacks:
emit EVERYTHING into a single proto package (``tiktok``) in a single
``tiktok.proto`` file. All message and enum names in this schema are globally
unique (betterproto relies on that), so a flat file compiles cleanly with
protoc and needs no cross-file imports.

Usage:
    python gen_proto.py <path-to-extracted-wheel> <output-dir>
"""

import dataclasses
import enum
import importlib
import pkgutil
import sys
from typing import Any, Dict, List, Set, Tuple


PROTO_PACKAGE = "tiktok"
ROOT_PACKAGE = "TikTokLiveProto.v3"


def import_all_modules(root_pkg_name: str) -> None:
    """Import every submodule under the root package so all classes load."""
    root = importlib.import_module(root_pkg_name)
    for mod in pkgutil.walk_packages(root.__path__, prefix=root_pkg_name + "."):
        try:
            importlib.import_module(mod.name)
        except Exception as ex:  # noqa: BLE001
            print(f"  [warn] skip {mod.name}: {ex}", file=sys.stderr)


def is_betterproto_message(cls: type) -> bool:
    try:
        import betterproto2
    except ImportError:
        return False
    return (
        isinstance(cls, type)
        and issubclass(cls, betterproto2.Message)
        and cls is not betterproto2.Message
        and dataclasses.is_dataclass(cls)
    )


def is_proto_enum(cls: type) -> bool:
    # betterproto2 enums subclass enum.IntEnum
    return isinstance(cls, type) and issubclass(cls, enum.IntEnum)


def collect_types(root_pkg_name: str) -> Tuple[Dict[str, type], Dict[str, type]]:
    """Return (messages, enums) keyed by simple class name."""
    messages: Dict[str, type] = {}
    enums: Dict[str, type] = {}
    root = importlib.import_module(root_pkg_name)
    modules = [root_pkg_name] + [
        m.name for m in pkgutil.walk_packages(root.__path__, prefix=root_pkg_name + ".")
    ]
    for mod_name in modules:
        try:
            mod = importlib.import_module(mod_name)
        except Exception:  # noqa: BLE001
            continue
        for name in dir(mod):
            obj = getattr(mod, name)
            if is_betterproto_message(obj):
                messages.setdefault(obj.__name__, obj)
            elif is_proto_enum(obj):
                # Skip stdlib / betterproto internal enums
                if obj.__module__.startswith(root_pkg_name) or "webcast" in obj.__module__.lower():
                    enums.setdefault(obj.__name__, obj)
    return messages, enums


# Map betterproto proto_type strings -> proto3 scalar type keywords.
SCALAR_MAP = {
    "bool": "bool",
    "int32": "int32",
    "int64": "int64",
    "uint32": "uint32",
    "uint64": "uint64",
    "sint32": "sint32",
    "sint64": "sint64",
    "fixed32": "fixed32",
    "fixed64": "fixed64",
    "sfixed32": "sfixed32",
    "sfixed64": "sfixed64",
    "float": "float",
    "double": "double",
    "string": "string",
    "bytes": "bytes",
}


def nested_type_name(field: dataclasses.Field, known: Set[str]) -> str:
    """
    Resolve the proto type name for a message/enum field. betterproto stores
    the python annotation; we resolve it to the referenced class's simple name.
    """
    ann = field.type
    # field.type may be a string (forward ref) or actual type
    if isinstance(ann, str):
        # Extract last identifier-ish token, strip Optional/list wrappers/quotes
        cleaned = ann.replace('"', "").replace("'", "")
        for token in ("Optional[", "list[", "List[", "]", "|", "None"):
            cleaned = cleaned.replace(token, " ")
        candidates = [c.strip() for c in cleaned.replace(".", " ").split() if c.strip()]
        for cand in reversed(candidates):
            if cand in known:
                return cand
        # fall back to last candidate
        return candidates[-1] if candidates else "bytes"
    # actual type object
    return getattr(ann, "__name__", "bytes")


def emit_field(field: dataclasses.Field, known: Set[str]) -> str:
    meta = field.metadata.get("betterproto")
    if meta is None:
        return ""
    number = meta.number
    ptype = meta.proto_type
    name = field.name

    # Map field (proto map<k,v>)
    if meta.map_meta is not None:
        km = meta.map_meta
        # map_meta has key_type / value_type as proto_type strings
        key_t = SCALAR_MAP.get(getattr(km, "key_type", "string"), "string")
        vtype_raw = getattr(km, "value_type", "string")
        if vtype_raw in SCALAR_MAP:
            val_t = SCALAR_MAP[vtype_raw]
        else:
            val_t = nested_type_name(field, known)
        return f"  map<{key_t}, {val_t}> {name} = {number};"

    # Determine the type string
    if ptype in SCALAR_MAP:
        type_str = SCALAR_MAP[ptype]
    elif ptype in ("message", "enum"):
        type_str = nested_type_name(field, known)
    else:
        type_str = "bytes"

    prefix = ""
    if meta.repeated:
        prefix = "repeated "
    elif meta.optional:
        prefix = "optional "

    return f"  {prefix}{type_str} {name} = {number};"


def emit_enum(cls: type) -> str:
    lines = [f"enum {cls.__name__} {{"]
    seen_values: Set[int] = set()
    # proto3 requires a zero value first; ensure one exists
    members = list(cls)
    has_zero = any(m.value == 0 for m in members)
    if not has_zero:
        lines.append(f"  {cls.__name__.upper()}_UNSPECIFIED = 0;")
    for m in members:
        # enum value names must be unique within the enclosing scope; prefix
        # with the enum name to avoid C++ scoping collisions across enums.
        val_name = f"{cls.__name__}_{m.name}"
        if m.value in seen_values:
            # proto3 needs allow_alias for duplicate values
            lines.insert(1, "  option allow_alias = true;")
            # only insert once
            seen_values.add(-1)
        seen_values.add(m.value)
        lines.append(f"  {val_name} = {m.value};")
    lines.append("}")
    return "\n".join(lines)


def emit_message(cls: type, known: Set[str]) -> str:
    lines = [f"message {cls.__name__} {{"]
    # group oneof fields
    oneofs: Dict[str, List[dataclasses.Field]] = {}
    plain: List[dataclasses.Field] = []
    for f in dataclasses.fields(cls):
        meta = f.metadata.get("betterproto")
        if meta is None:
            continue
        if meta.group:
            oneofs.setdefault(meta.group, []).append(f)
        else:
            plain.append(f)
    for f in plain:
        line = emit_field(f, known)
        if line:
            lines.append(line)
    for group_name, gfields in oneofs.items():
        lines.append(f"  oneof {group_name} {{")
        for f in gfields:
            meta = f.metadata["betterproto"]
            ptype = meta.proto_type
            if ptype in SCALAR_MAP:
                type_str = SCALAR_MAP[ptype]
            else:
                type_str = nested_type_name(f, known)
            lines.append(f"    {type_str} {f.name} = {meta.number};")
        lines.append("  }")
    lines.append("}")
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: gen_proto.py <extracted-wheel-dir> <out-dir>", file=sys.stderr)
        sys.exit(1)
    wheel_dir, out_dir = sys.argv[1], sys.argv[2]
    sys.path.insert(0, wheel_dir)

    print(f"[*] Importing all modules under {ROOT_PACKAGE} ...", file=sys.stderr)
    import_all_modules(ROOT_PACKAGE)

    print("[*] Collecting message & enum types ...", file=sys.stderr)
    messages, enums = collect_types(ROOT_PACKAGE)
    print(f"    {len(messages)} messages, {len(enums)} enums", file=sys.stderr)

    known: Set[str] = set(messages.keys()) | set(enums.keys())

    parts: List[str] = []
    parts.append('syntax = "proto3";')
    parts.append(f"package {PROTO_PACKAGE};")
    parts.append("")
    parts.append("// AUTO-GENERATED from the TikTokLiveProto v3 betterproto2 package.")
    parts.append("// Do not edit by hand; regenerate with tools/gen_proto.py.")
    parts.append("")

    for name in sorted(enums):
        parts.append(emit_enum(enums[name]))
        parts.append("")

    for name in sorted(messages):
        parts.append(emit_message(messages[name], known))
        parts.append("")

    import os
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "tiktok.proto")
    with open(out_path, "w") as fh:
        fh.write("\n".join(parts))
    print(f"[*] Wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
