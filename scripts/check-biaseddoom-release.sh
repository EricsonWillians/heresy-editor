#!/usr/bin/env bash

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: check-biaseddoom-release.sh [OPTIONS] [BIASEDDOOM_REPOSITORY]

Verify the pinned BiasedDoom release, source revision, GZDoom baseline, profile
annotation, documentation, and map-metadata audit.  Without a local repository,
the verifier fetches only the pinned tag and baseline into a temporary checkout.

Options:
  --check-latest   Require the pin to match GitHub's latest published release.
  --remote         Use a temporary minimal fetch instead of a local checkout.
  --manifest PATH  Read an alternate release contract (primarily for testing).
  -h, --help       Show this help.

The local repository defaults to $HERESY_BIASEDDOOM_SOURCE, a sibling
BiasedDoom checkout, or a temporary minimal fetch, in that order.
EOF
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
heresy_root=$(cd -- "$script_dir/.." && pwd)
manifest="$heresy_root/ports/biaseddoom.release"
check_latest=0
force_remote=0
biaseddoom_repo=

while (( $# > 0 )); do
	case $1 in
		--check-latest)
			check_latest=1
			shift
			;;
		--remote)
			force_remote=1
			shift
			;;
		--manifest)
			if (( $# < 2 )); then
				printf 'error: --manifest requires a path\n' >&2
				exit 2
			fi
			manifest=$2
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		-*)
			printf 'error: unknown option: %s\n' "$1" >&2
			usage >&2
			exit 2
			;;
		*)
			if [[ -n $biaseddoom_repo ]]; then
				printf 'error: only one BiasedDoom repository may be supplied\n' >&2
				exit 2
			fi
			biaseddoom_repo=$1
			shift
			;;
	esac
done

if (( force_remote != 0 )) && [[ -n $biaseddoom_repo ]]; then
	printf 'error: --remote cannot be combined with a local repository\n' >&2
	exit 2
fi

if [[ ! -r $manifest ]]; then
	printf 'error: release contract not found: %s\n' "$manifest" >&2
	exit 2
fi

release_repository=
release_upstream=
release_tag=
release_commit=
release_baseline=

while IFS='=' read -r key value || [[ -n $key ]]; do
	case $key in
		''|'#'*) continue ;;
		repository) release_repository=$value ;;
		upstream) release_upstream=$value ;;
		tag) release_tag=$value ;;
		commit) release_commit=$value ;;
		baseline) release_baseline=$value ;;
		*)
			printf 'error: unknown release-contract key: %s\n' "$key" >&2
			exit 2
			;;
	esac
done < "$manifest"

if [[ ! $release_repository =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ||
		! $release_upstream =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ||
		! $release_tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ||
		! $release_commit =~ ^[0-9a-f]{40}$ ||
		! $release_baseline =~ ^[0-9a-f]{40}$ ]]; then
	printf 'error: malformed BiasedDoom release contract: %s\n' "$manifest" >&2
	exit 2
fi

printf 'Release contract: %s %s\n' "$release_repository" "$release_tag"

if (( check_latest != 0 )); then
	if ! command -v gh >/dev/null 2>&1; then
		printf 'error: gh is required for --check-latest\n' >&2
		exit 2
	fi
	latest_tag=$(gh api "repos/$release_repository/releases/latest" --jq .tag_name)
	if [[ $latest_tag != "$release_tag" ]]; then
		printf 'error: latest BiasedDoom release is %s; profile is pinned to %s\n' \
			"$latest_tag" "$release_tag" >&2
		exit 1
	fi
	printf 'Latest published release: %s\n' "$latest_tag"
fi

temporary_root=
cleanup()
{
	if [[ -n $temporary_root && -d $temporary_root ]]; then
		rm -rf -- "$temporary_root"
	fi
}
trap cleanup EXIT

if [[ -z $biaseddoom_repo && $force_remote == 0 ]]; then
	if [[ -n ${HERESY_BIASEDDOOM_SOURCE:-} ]]; then
		biaseddoom_repo=$HERESY_BIASEDDOOM_SOURCE
	elif git -C "$heresy_root/../BiasedDoom" rev-parse --is-inside-work-tree \
			>/dev/null 2>&1; then
		biaseddoom_repo=$heresy_root/../BiasedDoom
	fi
fi

if [[ -z $biaseddoom_repo ]]; then
	temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/heresy-biaseddoom.XXXXXXXX")
	biaseddoom_repo=$temporary_root/BiasedDoom
	git init --quiet "$biaseddoom_repo"
	git -C "$biaseddoom_repo" remote add origin \
		"https://github.com/$release_repository.git"
	git -C "$biaseddoom_repo" fetch --quiet --depth=1 origin \
		"refs/tags/$release_tag:refs/tags/$release_tag"
	git -C "$biaseddoom_repo" remote add gzdoom \
		"https://github.com/$release_upstream.git"
	git -C "$biaseddoom_repo" fetch --quiet --depth=1 gzdoom "$release_baseline"
fi

if ! git -C "$biaseddoom_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	printf 'error: not a Git repository: %s\n' "$biaseddoom_repo" >&2
	exit 2
fi

tag_commit=$(git -C "$biaseddoom_repo" rev-parse --verify "$release_tag^{commit}")
if [[ $tag_commit != "$release_commit" ]]; then
	printf 'error: %s resolves to %s; expected %s\n' \
		"$release_tag" "$tag_commit" "$release_commit" >&2
	exit 1
fi
if ! git -C "$biaseddoom_repo" cat-file -e "$release_baseline^{commit}" 2>/dev/null; then
	printf 'error: pinned GZDoom baseline is unavailable: %s\n' \
		"$release_baseline" >&2
	exit 1
fi

if [[ $(git -C "$biaseddoom_repo" rev-parse --is-shallow-repository) == false ]] &&
		git -C "$biaseddoom_repo" rev-parse --verify gzdoom/master \
			>/dev/null 2>&1; then
	actual_baseline=$(git -C "$biaseddoom_repo" merge-base \
		"$release_commit" gzdoom/master)
	if [[ $actual_baseline != "$release_baseline" ]]; then
		printf 'error: pinned baseline is %s; merge base is %s\n' \
			"$release_baseline" "$actual_baseline" >&2
		exit 1
	fi
fi

version=${release_tag#v}
short_commit=${release_commit:0:9}
short_baseline=${release_baseline:0:9}
require_text()
{
	local expected=$1
	local path=$2
	if ! grep -Fq "$expected" "$path"; then
		printf 'error: release annotation is missing from %s: %s\n' \
			"${path#"$heresy_root/"}" "$expected" >&2
		exit 1
	fi
}
require_text "BiasedDoom $version ($short_commit)" \
	"$heresy_root/ports/biaseddoom.ugh"
require_text "$release_commit" "$heresy_root/docs/BiasedDoomInventory.txt"
require_text "$release_baseline" "$heresy_root/docs/BiasedDoomInventory.txt"
require_text "base ($short_baseline)" "$heresy_root/ports/biaseddoom.ugh"
printf 'Profile and inventory release pins: synchronized\n'

"$script_dir/audit-biaseddoom-metadata.sh" "$biaseddoom_repo" \
	"$release_baseline" "$release_commit"
if (( check_latest != 0 )); then
	printf 'Release synchronization result: compatible and current\n'
else
	printf 'Release synchronization result: pinned release is compatible\n'
fi
