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
    # the listings fetched by the tests are cached here, not in the user's home
    export XDG_CACHE_HOME="$REG_CONFIG/cache"

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

# `image delete` removes a *tag*, never a digest: other tags may point at the
# same manifest. It resolves its target through the listing service, so the
# record has to be registered with listing_mock before it can be deleted.
@test "uenv image delete removes the tag from the registry" {
    # real uenv images are always named store.squashfs: push that name here too.
    local sqfs=$TMP/store.squashfs
    cp "$SQFS_LIB/apptool/standalone/tool.squashfs" "$sqfs"

    run uenv image push "$sqfs" "test::del/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success

    local repo="${REG_PREFIX}/test/arapiles/zen3/del/1.0"
    local digest
    digest="$(registry_ctl digest "$REG_PORT" "$repo" v1)"
    [ -n "$digest" ]
    listing_mock add "$LISTING_FILE" \
        --path "test/arapiles/zen3/del/1.0/v1" --sha "$digest" \
        --size "$(stat -c%s "$sqfs")"

    # deletion is never anonymous, so the CLI insists on full credentials. The
    # test registry is anonymous and ignores them, but the guard still applies.
    local token=$TMP/token
    echo "dummy-token" > "$token"

    run uenv image delete --username=tester --token="$token" \
        "test::del/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success
    assert_output --partial "deleted"

    # the tag no longer resolves
    run registry_ctl digest "$REG_PORT" "$repo" v1
    log "${output}"
    assert_failure
}

# --token is not the only way to authenticate a delete: it resolves credentials
# through the same chain as push and pull (--token, then the uenv token store,
# then ~/.docker/config.json), all keyed on the host of registry.url.
@test "uenv image delete uses a token from the uenv token store" {
    local sqfs=$TMP/store.squashfs
    cp "$SQFS_LIB/apptool/standalone/tool.squashfs" "$sqfs"

    run uenv image push "$sqfs" "test::store/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success

    local repo="${REG_PREFIX}/test/arapiles/zen3/store/1.0"
    local digest
    digest="$(registry_ctl digest "$REG_PORT" "$repo" v1)"
    [ -n "$digest" ]
    listing_mock add "$LISTING_FILE" \
        --path "test/arapiles/zen3/store/1.0/v1" --sha "$digest" \
        --size "$(stat -c%s "$sqfs")"

    # the store is keyed by the registry's host[:port], not by a url.
    mkdir -p "$XDG_CONFIG_HOME/uenv/tokens"
    echo "dummy-token" > "$XDG_CONFIG_HOME/uenv/tokens/127.0.0.1:${REG_PORT}"
    chmod 600 "$XDG_CONFIG_HOME/uenv/tokens/127.0.0.1:${REG_PORT}"

    run uenv image delete --username=tester "test::store/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success
    assert_output --partial "deleted"

    run registry_ctl digest "$REG_PORT" "$repo" v1
    log "${output}"
    assert_failure
}

# with no source of credentials at all, delete goes ahead anonymously and lets
# the registry decide, exactly as push and pull do. The test registry allows it;
# a real one answers 401 and the error is reported.
@test "uenv image delete without credentials is left to the registry" {
    local sqfs=$TMP/store.squashfs
    cp "$SQFS_LIB/apptool/standalone/tool.squashfs" "$sqfs"

    run uenv image push "$sqfs" "test::anon/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success

    local repo="${REG_PREFIX}/test/arapiles/zen3/anon/1.0"
    local digest
    digest="$(registry_ctl digest "$REG_PORT" "$repo" v1)"
    [ -n "$digest" ]
    listing_mock add "$LISTING_FILE" \
        --path "test/arapiles/zen3/anon/1.0/v1" --sha "$digest" \
        --size "$(stat -c%s "$sqfs")"

    # remove every source: the token store entry another test may have written,
    # and the docker config (pointed at an empty directory so that the user's
    # own ~/.docker/config.json is never consulted).
    rm -f "$XDG_CONFIG_HOME/uenv/tokens/127.0.0.1:${REG_PORT}"
    export DOCKER_CONFIG=$TMP/docker
    mkdir -p "$DOCKER_CONFIG"

    run uenv image delete "test::anon/1.0:v1@arapiles%zen3"
    log "${output}"
    assert_success
    assert_output --partial "deleted"

    run registry_ctl digest "$REG_PORT" "$repo" v1
    log "${output}"
    assert_failure
}
