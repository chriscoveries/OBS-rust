#!/bin/bash
# Run a task in Docker the way zackees/bosn#666 specifies, until bosn does it
# itself. Each run gets an ephemeral container with a deterministic name; its
# build state lives in named volumes that the next run reattaches.
#
#   docker-task.sh run --name NAME [options] -- CMD...
#   docker-task.sh gc  --name NAME [--image IMAGE] [--apply] [--apply-legacy]
#
# run:
#   --name NAME             task name. Container: NAME-<checkout id>, where the
#                           checkout id is the first 12 hex of sha256(repo path).
#                           The name is the singleton lock: one container per
#                           task and checkout. A running one refuses the run
#                           (exit 75), or --wait waits for it to exit (docker
#                           wait, not polling); a stopped leftover is removed.
#   --repo PATH             checkout (default: the git top level of $PWD)
#   --image IMAGE           image repository; with --dockerfile it is built
#                           and tagged IMAGE:<content hash>, and only the newest
#                           --keep-images (default 3) tags of IMAGE are kept
#   --dockerfile FILE --context DIR [--build-arg K=V]...
#   --source ro|rw|overlay  how the checkout is mounted (default ro):
#                           ro: read-only bind; rw: read-write bind;
#                           overlay: read-only lower layer + a throwaway upper
#                           layer, so writes succeed and vanish after the run
#   --source-dest PATH      where the checkout appears (default /src)
#   --stack-volume VOL:DEST[:NAME]
#                           per-checkout volume, NAME-VOL-<checkout id> unless
#                           NAME given; reattached every run
#   --machine-volume VOL:DEST
#                           shared volume NAME-VOL, for caches that lock
#                           (ccache, cargo registry)
#   --mount SRC:DEST[:ro]   extra bind mount
#   --env K=V               environment for the task
#   --prune-cargo DIR       after every run, keep only the newest artifacts in
#                           the cargo target dir DIR (container path) and each
#                           DIR/<triple>: soldr cache prune-target
#                           --keep-latest. Safe when each target is built one
#                           way (one profile, one feature set), as these tasks
#                           are. soldr comes from a shared tools volume
#                           (docker-task-soldr-<version>), fetched once at the
#                           version and SHA256 cmake/common/soldr.cmake pins.
#   --budget VOL=SIZE       cap a stack or machine volume (SIZE: bytes, or with
#                           K, M or G): after every run, a volume over its budget
#                           is removed, so the next run starts it cold. Bounds
#                           what would otherwise only ever grow (cargo targets,
#                           compiler caches). Measured at most once an hour per
#                           volume (DOCKER_TASK_BUDGET_SECS, default 3600), all
#                           due volumes at once, so most runs pay nothing.
#   --clean                 remove this checkout's stack volumes first
#   --clean-all             also remove the machine volumes
#   --keep                  keep the container after the run (no --rm)
#   --wait                  wait for a running container of the same name
#
# gc: lists, and with --apply removes, what the runs leave behind: stack
# volumes labelled with a checkout path that no longer exists, stopped
# containers of the task, and IMAGE tags beyond the newest three. Volumes
# made before labels existed (NAME-*-<12 hex>, unlabelled) are only listed;
# --apply-legacy removes those not belonging to this checkout. Volumes in use
# by a container are never removed.
set -euo pipefail
# Possibly-empty arrays expand as ${a[@]+"${a[@]}"}: bash 3.2 (macOS) with
# set -u errors on "${a[@]}" when a is empty.

die() { echo "docker-task: $*" >&2; exit 2; }
label=io.github.obs-rust.docker-task

checkout_id() {
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$1" | sha256sum | cut -c1-12
  else
    printf '%s' "$1" | shasum -a 256 | cut -c1-12
  fi
}

# Prints the output only; a missing object prints nothing.
quiet() { "$@" 2>/dev/null || true; }

volume_in_use() { [ -n "$(quiet docker ps -aq --filter "volume=$1")" ]; }

to_bytes() { # 512, 300K, 20M, 8G
  local n=${1%[KkMmGg]} unit=${1##*[0-9]}
  case $unit in
  K | k) echo $((n * 1024)) ;;
  M | m) echo $((n * 1024 * 1024)) ;;
  G | g) echo $((n * 1024 * 1024 * 1024)) ;;
  *) echo "$n" ;;
  esac
}

# Removes VOLUME when it holds more than BUDGET bytes (measured with du in
# a throwaway container of IMAGE), unless a container still uses it.
enforce_budget() { # VOLUME BUDGET IMAGE
  local size
  size=$(quiet docker run --rm -v "$1:/v:ro" --entrypoint du "$3" -sb /v | cut -f1)
  [ -n "$size" ] || return 0
  [ "$size" -gt "$2" ] || return 0
  if volume_in_use "$1"; then
    echo "docker-task: $1 is over budget ($size > $2 bytes) but in use; left" >&2
    return 0
  fi
  docker volume rm "$1" >/dev/null 2>&1 &&
    echo "docker-task: $1 was over budget ($size > $2 bytes): removed, the next run starts it cold" >&2
}

ensure_volume() { # NAME SCOPE REPO
  if [ -z "$(quiet docker volume inspect -f '{{.Name}}' "$1")" ]; then
    docker volume create --label "$label.task=$task" --label "$label.scope=$2" \
      --label "$label.checkout=$3" "$1" >/dev/null
  fi
}

remove_volume() {
  if volume_in_use "$1"; then
    echo "docker-task: $1 is in use, not removed" >&2
    return 0
  fi
  docker volume rm "$1" >/dev/null 2>&1 || true
}

# Content hash of the Dockerfile, its context and the build arguments: the
# image tag, so an unchanged image is never rebuilt.
image_hash() {
  local dockerfile=$1 context=$2
  shift 2
  {
    cat "$dockerfile"
    (cd "$context" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 cat)
    printf '%s\n' "$@"
  } | if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi | cut -c1-16
}

prune_images() { # IMAGE KEEP APPLY
  local tags
  tags=$(quiet docker image ls "$1" --format '{{.CreatedAt}}\t{{.Tag}}' | LC_ALL=C sort -r | cut -f2 | grep -v '^<none>$' || true)
  local n=0 tag
  for tag in $tags; do
    n=$((n + 1))
    [ "$n" -le "$2" ] && continue
    if [ "$3" = 1 ]; then
      docker image rm "$1:$tag" >/dev/null 2>&1 && echo "removed image $1:$tag" || echo "kept image $1:$tag (in use)"
    else
      echo "image $1:$tag (beyond the newest $2)"
    fi
  done
}

# A shared volume holding the soldr binary, at the version and SHA256
# cmake/common/soldr.cmake pins, fetched once with the task image's curl.
ensure_soldr_tools() { # IMAGE
  local here soldr_cmake version arch triple sha vol
  here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  soldr_cmake="$here/../cmake/common/soldr.cmake"
  version=$(sed -n 's/^set(OBS_SOLDR_VERSION \([0-9.]*\))$/\1/p' "$soldr_cmake")
  arch=$(quiet docker info -f '{{.Architecture}}')
  case $arch in aarch64 | arm64) triple=aarch64-unknown-linux-gnu ;; *) triple=x86_64-unknown-linux-gnu ;; esac
  sha=$(sed -n "s/^set(_obs_soldr_sha256_$triple \([0-9a-f]*\))$/\1/p" "$soldr_cmake")
  vol="docker-task-soldr-$version"
  if [ -z "$(quiet docker volume inspect -f '{{.Name}}' "$vol")" ]; then
    docker volume create --label "$label.scope=tools" "$vol" >/dev/null
  fi
  docker run --rm -v "$vol:/t" --entrypoint sh "$1" -c '
    [ -x /t/soldr ] && exit 0
    url=https://github.com/zackees/soldr/releases/download/v$0/soldr-v$0-$1.tar.zst
    curl -fsSL "$url" -o /tmp/soldr.tar.zst &&
      echo "$2  /tmp/soldr.tar.zst" | sha256sum -c - >/dev/null &&
      tar --zstd -xf /tmp/soldr.tar.zst -C /t soldr' "$version" "$triple" "$sha" >&2 ||
    echo "docker-task: could not fetch soldr $version for pruning" >&2
  echo "$vol"
}

cmd_run() {
  local repo="" image="" dockerfile="" context="" source_mode=ro source_dest=/src
  local keep_images=3 clean=0 keep=0 wait=0
  local -a build_args=() stack=() machine=() mounts=() envs=() budgets=() prune=()
  while [ $# -gt 0 ]; do
    case $1 in
    --name) task=$2; shift 2 ;;
    --repo) repo=$2; shift 2 ;;
    --image) image=$2; shift 2 ;;
    --dockerfile) dockerfile=$2; shift 2 ;;
    --context) context=$2; shift 2 ;;
    --build-arg) build_args+=("$2"); shift 2 ;;
    --keep-images) keep_images=$2; shift 2 ;;
    --source) source_mode=$2; shift 2 ;;
    --source-dest) source_dest=$2; shift 2 ;;
    --stack-volume) stack+=("$2"); shift 2 ;;
    --machine-volume) machine+=("$2"); shift 2 ;;
    --mount) mounts+=("$2"); shift 2 ;;
    --env) envs+=("$2"); shift 2 ;;
    --budget) budgets+=("$2"); shift 2 ;;
    --prune-cargo) prune+=("$2"); shift 2 ;;
    --clean) clean=1; shift ;;
    --clean-all) clean=2; shift ;;
    --keep) keep=1; shift ;;
    --wait) wait=1; shift ;;
    --) shift; break ;;
    *) die "unknown option $1" ;;
    esac
  done
  [ -n "$task" ] || die "--name is required"
  [ -n "$image" ] || die "--image is required"
  [ $# -gt 0 ] || die "no command after --"
  case $source_mode in ro | rw | overlay) ;; *) die "--source must be ro, rw or overlay" ;; esac
  [ -n "$repo" ] || repo=$(git rev-parse --show-toplevel)
  repo=$(cd "$repo" && pwd -P)
  local id cname
  id=$(checkout_id "$repo")
  cname="$task-$id"

  # The image: built only when its content hash is new, capped per task.
  # (tag and the overlay_* names are global: the EXIT trap reads them after
  # this function has returned.)
  tag="$image"
  if [ -n "$dockerfile" ]; then
    [ -n "$context" ] || context=$(dirname "$dockerfile")
    tag="$image:$(image_hash "$dockerfile" "$context" ${build_args[@]+"${build_args[@]}"})"
    if [ -z "$(quiet docker image inspect -f '{{.Id}}' "$tag")" ]; then
      local -a ba=()
      local a
      for a in ${build_args[@]+"${build_args[@]}"}; do ba+=(--build-arg "$a"); done
      docker build -q ${ba[@]+"${ba[@]}"} -t "$tag" -f "$dockerfile" "$context" >/dev/null
    fi
    prune_images "$image" "$keep_images" 1 >/dev/null
  fi

  # The singleton: one container per task and checkout.
  local state
  state=$(quiet docker container inspect -f '{{.State.Status}}' "$cname")
  case $state in
  "") ;;
  running | restarting | paused)
    if [ "$wait" = 1 ]; then
      echo "docker-task: waiting for $cname to finish" >&2
      docker wait "$cname" >/dev/null 2>&1 || true
    else
      echo "docker-task: $cname is already running (one run per task and checkout; --wait to queue)" >&2
      exit 75
    fi
    ;;
  *) docker rm -f "$cname" >/dev/null 2>&1 || true ;;
  esac

  local -a args=(--name "$cname")
  [ "$keep" = 1 ] || args+=(--rm)

  local spec vol dest name
  local -a volume_names=()
  for spec in ${stack[@]+"${stack[@]}"}; do
    IFS=: read -r vol dest name <<<"$spec"
    name=${name:-$task-$vol-$id}
    volume_names+=("$vol=$name")
    [ "$clean" -ge 1 ] && remove_volume "$name"
    ensure_volume "$name" stack "$repo"
    args+=(-v "$name:$dest")
  done
  for spec in ${machine[@]+"${machine[@]}"}; do
    IFS=: read -r vol dest <<<"$spec"
    name=$task-$vol
    volume_names+=("$vol=$name")
    [ "$clean" -ge 2 ] && remove_volume "$name"
    ensure_volume "$name" machine ""
    args+=(-v "$name:$dest")
  done
  for spec in ${mounts[@]+"${mounts[@]}"}; do args+=(-v "$spec"); done
  for spec in ${envs[@]+"${envs[@]}"}; do args+=(-e "$spec"); done

  # The checkout.
  overlay_dir="" overlay_vol=""
  case $source_mode in
  ro) args+=(-v "$repo:$source_dest:ro") ;;
  rw) args+=(-v "$repo:$source_dest") ;;
  overlay)
    overlay_dir=$(mktemp -d "${TMPDIR:-/tmp}/docker-task-overlay.XXXXXX")
    mkdir "$overlay_dir/upper" "$overlay_dir/work"
    overlay_vol="$cname-overlay"
    docker volume rm "$overlay_vol" >/dev/null 2>&1 || true
    docker volume create --driver local --opt type=overlay --opt device=overlay \
      --opt "o=lowerdir=$repo,upperdir=$overlay_dir/upper,workdir=$overlay_dir/work" \
      --label "$label.task=$task" --label "$label.scope=overlay" "$overlay_vol" >/dev/null
    args+=(-v "$overlay_vol:$source_dest")
    ;;
  esac

  cleanup_overlay() {
    [ -n "$overlay_vol" ] || return 0
    docker volume rm "$overlay_vol" >/dev/null 2>&1 || true
    # The container wrote the upper layer as root: clear it from a container,
    # in the background so the caller doesn't wait for it.
    (
      docker run --rm -v "$overlay_dir:/overlay" --entrypoint rm "$tag" -rf /overlay/upper /overlay/work >/dev/null 2>&1 || true
      rmdir "$overlay_dir" 2>/dev/null || true
    ) </dev/null >/dev/null 2>&1 &
  }
  trap cleanup_overlay EXIT

  # The prune runs in the same container, after the command, whatever its
  # exit status; the command's status is kept.
  if [ "${#prune[@]}" -gt 0 ]; then
    local tools
    tools=$(ensure_soldr_tools "$tag")
    args+=(-v "$tools:/opt/docker-task-soldr:ro" -e "DOCKER_TASK_PRUNE=${prune[*]}")
    set -- sh -c 'status=0; "$@" || status=$?
for dir in $DOCKER_TASK_PRUNE; do
  [ -d "$dir" ] || continue
  for t in "$dir" "$dir"/*-*-*; do
    [ -d "$t" ] || continue
    /opt/docker-task-soldr/soldr cache prune-target --keep-latest --force --allow-unpinned --json "$t" 2>/dev/null |
      sed -n "s/.*\"reclaimed_human\": *\"\([^\"]*\)\".*/docker-task: pruned \1 from ${t##*/}/p" >&2 || true
  done
done
exit $status' docker-task-prune "$@"
  fi

  local status=0
  docker run "${args[@]}" "$tag" "$@" || status=$?

  # Budgets, after any run (a failed one grows the volumes too), at most
  # once per DOCKER_TASK_BUDGET_SECS per volume; due volumes in parallel.
  local budget entry stamp now state
  state="${XDG_STATE_HOME:-$HOME/.local/state}/docker-task"
  mkdir -p "$state"
  now=$(date +%s)
  for budget in ${budgets[@]+"${budgets[@]}"}; do
    vol=${budget%%=*}
    for entry in ${volume_names[@]+"${volume_names[@]}"}; do
      [ "${entry%%=*}" = "$vol" ] || continue
      stamp="$state/${entry#*=}.budget-checked"
      if [ -f "$stamp" ] && [ $((now - $(cat "$stamp"))) -lt "${DOCKER_TASK_BUDGET_SECS:-3600}" ]; then
        continue
      fi
      echo "$now" >"$stamp"
      enforce_budget "${entry#*=}" "$(to_bytes "${budget#*=}")" "$tag" &
    done
  done
  wait
  return "$status"
}

cmd_gc() {
  local image="" apply=0 apply_legacy=0 repo=""
  while [ $# -gt 0 ]; do
    case $1 in
    --name) task=$2; shift 2 ;;
    --image) image=$2; shift 2 ;;
    --repo) repo=$2; shift 2 ;;
    --apply) apply=1; shift ;;
    --apply-legacy) apply_legacy=1; shift ;;
    *) die "unknown option $1" ;;
    esac
  done
  [ -n "$task" ] || die "--name is required"
  [ -n "$repo" ] || repo=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
  local own_id
  own_id=$(checkout_id "$(cd "$repo" && pwd -P)")

  local v path
  for v in $(quiet docker volume ls -q --filter "label=$label.task=$task" --filter "label=$label.scope=stack"); do
    path=$(quiet docker volume inspect -f "{{index .Labels \"$label.checkout\"}}" "$v")
    [ -d "$path" ] && continue
    if volume_in_use "$v"; then
      echo "volume $v (checkout $path gone, but in use)"
    elif [ "$apply" = 1 ]; then
      docker volume rm "$v" >/dev/null && echo "removed volume $v (checkout $path gone)"
    else
      echo "volume $v (checkout $path gone)"
    fi
  done
  for v in $(quiet docker volume ls -q | grep -E "^$task-.+-[0-9a-f]{12}$" || true); do
    [ -n "$(quiet docker volume inspect -f "{{index .Labels \"$label.task\"}}" "$v")" ] && continue
    case $v in *-"$own_id") continue ;; esac
    if volume_in_use "$v"; then
      echo "legacy volume $v (in use)"
    elif [ "$apply_legacy" = 1 ]; then
      docker volume rm "$v" >/dev/null && echo "removed legacy volume $v"
    else
      echo "legacy volume $v (unlabelled, another checkout; --apply-legacy removes)"
    fi
  done
  local c
  for c in $(quiet docker ps -aq --filter "name=^$task-" --filter status=exited) $(quiet docker ps -aq --filter "name=^$task-" --filter status=created); do
    if [ "$apply" = 1 ]; then
      docker rm "$c" >/dev/null && echo "removed container $c"
    else
      echo "container $c (stopped)"
    fi
  done
  [ -z "$image" ] || prune_images "$image" 3 "$apply"
}

task="" tag="" overlay_dir="" overlay_vol=""
case ${1:-} in
run) shift; cmd_run "$@" ;;
gc) shift; cmd_gc "$@" ;;
*) die "usage: docker-task.sh run|gc --name NAME ... (see the header)" ;;
esac
