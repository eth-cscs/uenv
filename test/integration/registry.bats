# CLI tests that drive `uenv push`/`pull` against a throwaway zot registry (a
# binary, no container). A listing_mock stands in for the CSCS listing service.
# The suite self-skips when no zot binary is available.
#
# The native OCI round-trip is covered by the [registry] unit cases, which start
# their own zot; this suite covers only the user-facing CLI path.
#
# Run from the build directory:  ./test/bats ./test/registry.bats

function setup_file() {
    load ./common
    if [[ -z "$(registry_ctl runtime)" ]]; then
        return 0  # tests self-skip
    fi

    export REG_PORT="$(registry_ctl free-port)"
    export REG_STATE="$(mktemp -u)"
    export REG_PREFIX="uenvtest"
    registry_ctl serve "$REG_STATE" "$REG_PORT" &
    registry_ctl wait "$REG_PORT" --timeout 30

    export LST_PORT="$(listing_mock free-port)"
    export LISTING_FILE="$(mktemp)"
    listing_mock serve "$LISTING_FILE" "$LST_PORT" &
    listing_mock wait-server "$LST_PORT" --timeout 5

    export REG_CONFIG="$(mktemp -d)"
    mkdir -p "$REG_CONFIG/uenv"
    cat > "$REG_CONFIG/uenv/config.toml" <<EOF
[registry]
url = "http://127.0.0.1:${REG_PORT}/${REG_PREFIX}"
default_namespace = "deploy"
listing_url = "http://127.0.0.1:${LST_PORT}/list"
EOF
}

function teardown_file() {
    load ./common
    if [[ -n "${REG_STATE:-}" ]]; then
        registry_ctl kill "$REG_STATE" 2>/dev/null || true
    fi
    if [[ -n "${LISTING_FILE:-}" ]]; then
        listing_mock kill "$LISTING_FILE" 2>/dev/null || true
    fi
    if [[ -n "${REG_CONFIG:-}" ]]; then
        rm -rf "$REG_CONFIG"
    fi
}

function setup() {
    set -u
    export CLUSTER_NAME=arapiles

    bats_load_library bats-support
    bats_load_library bats-assert
    load ./common

    export PATH="$UENV_BIN_PATH:$PATH"
    unset UENV_MOUNT_LIST
    unset -f uenv

    if [[ -z "$(registry_ctl runtime)" ]]; then
        skip "no zot binary available"
    fi

    export XDG_CONFIG_HOME="$REG_CONFIG"

    export TMP=$DATA/scratch
    rm -rf $TMP
    mkdir -p $TMP
}

function teardown() {
    :
}

@test "uenv push then pull round-trips via the registry" {
    local sqfs=$SQFS_LIB/apptool/standalone/tool.squashfs
    [ -f "$sqfs" ]

    run uenv image push "$sqfs" "deploy::app/1.0:v1@arapiles%zen3"
    log "${output}"
    [ "${status}" -eq 0 ]

    # the image is hashed, then uploaded: each phase reports its own progress
    # bar. Even with no tty, barkeep prints the bar's first and last line.
    assert_output --partial "validating"
    assert_output --partial "pushing"

    # register a listing record so pull can resolve the label
    local repo="${REG_PREFIX}/deploy/arapiles/zen3/app/1.0"
    local digest
    digest="$(registry_ctl digest "$REG_PORT" "$repo" v1)"
    [ -n "$digest" ]
    local size
    size="$(stat -c%s "$sqfs")"
    listing_mock add "$LISTING_FILE" \
        --path "deploy/arapiles/zen3/app/1.0/v1" --sha "$digest" --size "$size"

    local RP=$TMP/repo
    run uenv repo create "$RP"
    log "${output}"
    [ "${status}" -eq 0 ]

    run uenv --repo "$RP" image pull "deploy::app/1.0:v1@arapiles%zen3"
    log "${output}"
    [ "${status}" -eq 0 ]

    run uenv --repo "$RP" image ls "app/1.0:v1@arapiles%zen3"
    log "${output}"
    [ "${status}" -eq 0 ]
    assert_output --partial "app"

    # the manifest fetched on pull is persisted alongside the image, keyed by
    # its own digest.
    [ -f "$RP/images/$digest/manifest.json" ]
    [ "$digest" = "$(sha256sum "$RP/images/$digest/manifest.json" | cut -d' ' -f1)" ]
}

@test "uenv image push reuses a locally-added image's manifest.json" {
    local sqfs=$SQFS_LIB/apptool/standalone/tool.squashfs
    [ -f "$sqfs" ]

    local RP=$TMP/repo
    run uenv repo create "$RP"
    assert_success

    run uenv --repo "$RP" image add tool/1.0:v1@arapiles%zen3 "$sqfs"
    assert_success

    local local_sha
    local_sha="$(uenv --repo "$RP" image inspect --format='{sha256}' tool/1.0:v1@arapiles%zen3)"
    [ -f "$RP/images/$local_sha/manifest.json" ]

    # push by label (not by file path): this resolves the source against the
    # local repo and must reuse its manifest.json verbatim, rather than
    # minting a fresh one.
    run uenv --repo "$RP" image push tool/1.0:v1@arapiles%zen3 "deploy::apptag/1.0:v1@arapiles%zen3"
    assert_success

    local repo="${REG_PREFIX}/deploy/arapiles/zen3/apptag/1.0"
    local registry_digest
    registry_digest="$(registry_ctl digest "$REG_PORT" "$repo" v1)"
    [ -n "$registry_digest" ]

    # the digest the registry stores the image under must be exactly the hash
    # it is already stored under locally - proof the manifest was reused, not
    # re-minted.
    [ "$registry_digest" = "$local_sha" ]

    # pulling it back into a fresh repo must also persist manifest.json,
    # byte-identical to the one minted by `image add`.
    listing_mock add "$LISTING_FILE" \
        --path "deploy/arapiles/zen3/apptag/1.0/v1" --sha "$registry_digest" \
        --size "$(stat -c%s "$sqfs")"

    local RP2=$TMP/repo2
    run uenv repo create "$RP2"
    assert_success

    run uenv --repo "$RP2" image pull "deploy::apptag/1.0:v1@arapiles%zen3"
    assert_success

    [ -f "$RP2/images/$registry_digest/manifest.json" ]
    diff "$RP/images/$local_sha/manifest.json" \
         "$RP2/images/$registry_digest/manifest.json"
}
