#!/usr/bin/env python3
"""Measure component coupling in the Voxelcraft source tree.

Reports three things the architecture review relies on:

* the component dependency matrix (rows depend on columns, counted by
  `#include` statements, self-edges excluded);
* mutual edges, i.e. the cycles the layering still tolerates;
* header fan-in, the headers that carry the most coupling.

Component membership comes from the directory name under `src/`, which is the
same unit `mk/components.mk` builds. The script reads source only; it needs no
build output and no third-party packages.
"""

import collections
import glob
import os
import re
import sys

COMPONENTS = ["core", "world", "space", "ecology", "gameplay", "presentation", "app"]
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def header_owners():
    """Map each project header basename to its owning component."""
    owner = {}
    for component in COMPONENTS:
        for path in glob.glob(f"src/{component}/*.h"):
            owner[os.path.basename(path)] = component
    return owner


def edges(owner, kinds):
    """Count cross-component includes; `kinds` selects .c and/or .h."""
    counts = collections.Counter()
    for component in COMPONENTS:
        for kind in kinds:
            pattern = f"src/{component}/*.{kind}"
            for path in glob.glob(pattern):
                with open(path, encoding="utf-8", errors="replace") as handle:
                    for line in handle:
                        match = INCLUDE.match(line)
                        if not match:
                            continue
                        target = owner.get(os.path.basename(match.group(1)))
                        if target and target != component:
                            counts[(component, target)] += 1
    return counts


def print_matrix(counts):
    print("rows depend on columns (include statements, self-edges excluded)")
    print(f"{'':<13}" + "".join(f"{c[:6]:>7}" for c in COMPONENTS))
    for source in COMPONENTS:
        row = "".join(f"{counts.get((source, target), 0):>7}" for target in COMPONENTS)
        print(f"{source:<13}{row}")


def print_cycles(counts):
    """A cycle is a pair of components that include each other."""
    print("\nmutual edges (cycles the layering still tolerates)")
    seen = set()
    for source, target in sorted(counts):
        if (target, source) not in counts or (target, source) in seen:
            continue
        seen.add((source, target))
        forward = counts[(source, target)]
        backward = counts[(target, source)]
        print(f"  {source} <-> {target}   ({forward} vs {backward})")


def print_fan_in(owner, limit=15):
    counts = collections.Counter()
    users = collections.defaultdict(set)
    for component in COMPONENTS:
        for path in glob.glob(f"src/{component}/*.c") + glob.glob(f"src/{component}/*.h"):
            with open(path, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    match = INCLUDE.match(line)
                    if not match:
                        continue
                    name = os.path.basename(match.group(1))
                    if name in owner:
                        counts[name] += 1
                        users[name].add(component)
    print(f"\ntop {limit} headers by fan-in")
    for name, count in counts.most_common(limit):
        print(f"  {count:>4}  {owner[name]:>12}/{name:<34} in {len(users[name])} components")


def main():
    if not os.path.isdir("src/app"):
        print("run this from the repository root", file=sys.stderr)
        return 2
    owner = header_owners()
    counts = edges(owner, ("c", "h"))
    print_matrix(counts)
    print_cycles(counts)
    print_fan_in(owner)
    return 0


if __name__ == "__main__":
    sys.exit(main())
