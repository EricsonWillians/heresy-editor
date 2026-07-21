#!/usr/bin/env bash

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: audit-biaseddoom-metadata.sh [BIASEDDOOM_REPOSITORY [BASELINE [HEAD]]]

Compare BiasedDoom's map-facing metadata with its GZDoom merge base.  The
repository defaults to ../BiasedDoom (or $HERESY_BIASEDDOOM_SOURCE), HEAD
defaults to HEAD, and BASELINE defaults to the merge base with gzdoom/master.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
	usage
	exit 0
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
heresy_root=$(cd -- "$script_dir/.." && pwd)
biaseddoom_repo=${1:-${HERESY_BIASEDDOOM_SOURCE:-"$heresy_root/../BiasedDoom"}}
baseline=${2:-}
head_ref=${3:-HEAD}

if ! git -C "$biaseddoom_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	printf 'error: not a Git repository: %s\n' "$biaseddoom_repo" >&2
	exit 2
fi

head_commit=$(git -C "$biaseddoom_repo" rev-parse --verify "$head_ref^{commit}")

if [[ -z $baseline ]]; then
	upstream_ref=
	for candidate in gzdoom/master remotes/gzdoom/master; do
		if git -C "$biaseddoom_repo" rev-parse --verify "$candidate^{commit}" >/dev/null 2>&1; then
			upstream_ref=$candidate
			break
		fi
	done
	if [[ -z $upstream_ref ]]; then
		printf '%s\n' \
			'error: no gzdoom/master ref; pass the matching upstream commit as BASELINE' >&2
		exit 2
	fi
	baseline=$(git -C "$biaseddoom_repo" merge-base "$head_commit" "$upstream_ref")
fi

baseline_commit=$(git -C "$biaseddoom_repo" rev-parse --verify "$baseline^{commit}")

metadata_paths=(
	"wadsrc/static/mapinfo"
	"src/gamedata/g_doomedmap.cpp"
	"src/gamedata/info.cpp"
	"src/gamedata/info.h"
	"src/playsim/p_lnspec.cpp"
	"src/playsim/p_lnspec.h"
	"src/gamedata/xlat"
	"wadsrc/static/xlat"
	"src/maploader/udmf.cpp"
	"specs/udmf.txt"
)

printf 'BiasedDoom repository: %s\n' "$biaseddoom_repo"
printf 'Baseline: %s\n' "$baseline_commit"
printf 'Head:     %s\n' "$head_commit"

changed_metadata=$(git -C "$biaseddoom_repo" diff --name-only \
	"$baseline_commit..$head_commit" -- "${metadata_paths[@]}")
changed_editor_numbers=$(git -C "$biaseddoom_repo" diff --name-only \
	-G'DoomEdNum|DoomEdNums|doomednum' "$baseline_commit..$head_commit" -- \
	'*.cpp' '*.h' '*.txt' '*.zs')

failed=0
if [[ -n $changed_metadata ]]; then
	printf '\nMap metadata sources changed and require manual review:\n%s\n' "$changed_metadata" >&2
	failed=1
else
	printf 'Canonical thing, special, translator, and UDMF sources: unchanged\n'
fi

if [[ -n $changed_editor_numbers ]]; then
	printf '\nDoomEdNum-related changes require manual review:\n%s\n' \
		"$changed_editor_numbers" >&2
	failed=1
else
	printf 'DoomEdNum registrations: unchanged\n'
fi

procgen_path=src/common/maps/procgen/procgen_udmf.cpp
if git -C "$biaseddoom_repo" cat-file -e "$head_commit:$procgen_path" 2>/dev/null; then
	procgen_source=$(git -C "$biaseddoom_repo" show "$head_commit:$procgen_path")
	if grep -Fq 'output = "namespace = \"zdoom\";' <<<"$procgen_source"; then
		printf 'Procedural generator namespace: zdoom\n'
	else
		printf 'Procedural generator namespace is not the audited zdoom value\n' >&2
		failed=1
	fi
	for special in 11 12 62 243; do
		if ! grep -Eq "(true|false),[[:space:]]*${special}," <<<"$procgen_source"; then
			printf 'Expected procedural-generator special %s was not found\n' "$special" >&2
			failed=1
		fi
	done
	printf 'Procedural generator specials checked: 11 12 62 243\n'
else
	printf 'Procedural generator: absent at this revision\n'
fi

if (( failed != 0 )); then
	printf '\nAudit result: review required\n' >&2
	exit 1
fi

printf 'Audit result: no BiasedDoom-only map identifiers detected\n'
