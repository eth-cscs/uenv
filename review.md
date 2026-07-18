# Code Review: `no-oras` branch

- **Date:** 2026-07-06
- **Scope:** `git diff main...no-oras` — 76 files, +6446/−1106
- **Subject:** Replacement of the external `oras` binary with a native OCI registry client (`src/oci/`), supporting a new sha256 implementation (`src/util/sha256`), generalized curl layer (`src/util/curl`), shared parse scaffolding (`src/util/parse`), reworked CLI `pull`/`push`/`copy`/`delete`, and a new `registry` integration test suite (zot + `listing_mock`).
- **Method:** 8 independent finder passes (line-by-line diff scan, removed-behavior audit, cross-file call tracing, reuse, simplification, efficiency, altitude, CLAUDE.md conventions) produced 45 raw candidates → deduplicated to 16 → each independently verified against the code. **All 16 were CONFIRMED.**

Positive results from the cross-file pass worth recording: the build compiles clean, all `registry_listing`/`make_temp_dir`/`oras` call sites were updated, meson/zlib linkage is consistent, and the `src/oci` isolation rule holds (`grep -rn '#include <\(uenv\|site\|cli\)/' src/oci/` returns nothing).

---

## Critical

### 1. Pull/copy address the registry by a manifest digest built from the squashfs file sha — production pulls will 404

**Where:** `src/cli/pull.cpp:208-214`, `src/cli/copy.cpp:194-199`

`record.sha` — the `sha256` field parsed from the CSCS listing service (`src/site/site.cpp:46,61`) — is passed through `oci::digest::sha256(record.sha.string())` and used as the **manifest** digest in `client->get_manifest(reference::digest(...))`.

But everywhere else in uenv, `record.sha` is the sha256 of the squashfs **file**:

- `validate_squashfs_image` (`src/cli/util.cpp:133`) computes `util::sha256_file`;
- `uenv image add` and the repo database key records by file hash;
- test fixtures (`test/setup/setup_repos.bash`) use `sha256sum ${sqfs}`;
- the new push path itself uses `sha256_file` as the **layer** blob digest (`src/oci/push.cpp:46`).

A layer digest can never equal the manifest digest (the hash of the manifest JSON). The contradiction is internal to the branch: `pull.cpp:152/166` still uses `record.sha` as the file-sha repo key (`uenv_paths(record.sha)`, `sha_in_repo` dedup) while line 208 treats the same value as a manifest digest — both cannot be true.

The old flow never used `record.sha` to address the registry: deleted `oras::pull_tag` pulled by tag (`.../name/version:tag`), with the sha used only for local store paths and dedup.

**Why the tests pass anyway:** `registry.bats:87-93` feeds `listing_mock` the manifest digest obtained from `registry_ctl digest` (and `listing_mock` documents `--sha` as "manifest sha256") — i.e. the test suite *redefines* the listing contract instead of matching the deployed service. No evidence was found that the real `uenv-list.svc.cscs.ch` returns manifest digests.

**Impact:** unless the listing service is changed in lockstep, `GET /v2/<repo>/manifests/sha256:<file-sha>` 404s and **every production `uenv image pull` / `uenv image copy` fails** with "unable to fetch the image manifest".

**Fix:** pull by tag as the old flow did (name/version:tag is available in the record), or explicitly change the listing-service contract and document the migration. This decision gates several other findings.

---

## High

### 2. Partial/corrupt download is left on disk and permanently registered as a valid uenv

**Where:** `src/oci/client.cpp:308-316` (`get_blob_to_file`), `src/cli/pull.cpp:161,176,291-328`

`get_blob_to_file` returns early on transport error (client.cpp:308-311) and on non-200 (312-316) **without removing the destination file**; only the digest-mismatch branch removes it (321-322). `util::curl::perform` opens the file with `fopen(..., "wb")` and streams whatever arrives into it — including partial content on a dropped connection and the registry's HTML/JSON error body on a 502 (`src/util/curl.cpp:405-418`).

At the CLI layer, the failure path (`src/cli/pull.cpp:291-300`) does no cleanup (only the `signal_exception` catch removes the store). On the next run, `sqfs_exists = fs::exists(paths.squashfs)` is true (pull.cpp:161), so `pull_sqfs` is false (pull.cpp:176) with **no digest check of the existing file**, the download is skipped, and the record is added to the repo (pull.cpp:319-328).

**Impact:** a mid-transfer network drop yields a permanently corrupt uenv that fails at mount time; no pull without `--force` ever repairs it. The old oras flow staged via an ingest directory, so this failure mode did not exist.

**Fix:** remove the output file on *all* error paths in `get_blob_to_file` (or download to a temp name and rename on digest success).

### 3. `uenv image delete` crashes (`std::bad_variant_access`) on the anonymous-credentials path

**Where:** `src/cli/delete.cpp:67-73`

`oci::resolve_credentials` documents and returns `success(std::nullopt)` when no credentials are found (`src/oci/auth.h:87`, `auth.cpp:329`). With no `--token`, no token-store file, and no docker config entry, `c` holds a *value* containing `nullopt`; `delete.cpp:70-71` then calls `c.error()` on the value-holding `util::expected` — `std::get<1>` on the wrong variant alternative (`src/util/expected.h:353-354`) → uncaught `std::bad_variant_access` → core dump. Even if `error()` were benign, the branch lacks `return 1`, so `(*c).value()` at line 73 would throw `std::bad_optional_access`.

`push.cpp:74-82` (and copy/pull) handle the same `nullopt` case correctly — this is a botched adaptation.

**Fix:** `if (!*c) { term::error("full credentials must be provided"); return 1; }`

### 4. Pull fails entirely on registries without the Referrers API — no fallback to the tag schema this branch itself writes

**Where:** `src/oci/pull.cpp:99-102`, `src/oci/client.cpp:403-406`

`client::referrers()` errors on any non-200 with no 404 special-case; `pull_meta` propagates it; the CLI treats it as fatal (`src/cli/pull.cpp:228-231`, exit 1 "unable to pull meta data"). Per the OCI spec, a 404 from `/v2/<repo>/referrers/<digest>` is the signal that the endpoint is unimplemented (implementing registries return 200 with an empty list).

The irony: the push side explicitly maintains the `sha256-<hex>` referrers-tag index "for registries that do not auto-index the Referrers API" (`src/oci/push.cpp:212-240`), but the read side never consults it. `copy_image` (push.cpp:413-420) shows the intended graceful degradation exists elsewhere in the same file. The old `oras discover` fell back to the tag schema transparently.

**Impact:** a uenv that pulled fine with the oras-based CLI becomes unpullable on pre-OCI-1.1 registries (older JFrog/Artifactory, docker registry:2) — the pull dies before even attempting the squashfs download.

**Fix:** on referrers failure (at minimum on 404), fall back to reading the `sha256-<hex>` tag index; degrade to "no metadata" rather than failing the pull.

### 5. `maintain_referrers_tag` clobbers existing referrers when the tag GET fails transiently

**Where:** `src/oci/push.cpp:220-236`

`if (auto existing = c.get_manifest(tag))` treats **any** failure — transient network error, expired-token 401, timeout, or a body that fails `parse_referrers` — the same as "tag absent", then PUTs an index containing only the new referrer. The caller cannot do better: `client::get_manifest` (`client.cpp:346-353`) returns a flat `expected<..., std::string>`, collapsing 404 (correct to start empty) and transport errors (should abort) into one opaque string.

**Impact:** a second `attach` during a network hiccup silently replaces the fallback referrers tag with a single-entry index, discarding all previously attached metadata — clients relying on the tag can no longer discover the original artifacts.

**Fix:** make `get_manifest` errors distinguishable (status-carrying error type); start empty only on 404, abort on anything else.

### 6. Bearer tokens are cached forever — long transfers fail at the end with 401, no re-auth

**Where:** `src/oci/client.cpp:192-215` (`token_for`), `src/oci/auth.cpp:49-58`

Tokens are fetched once per client and cached unconditionally; `parse_token_response` extracts only `token`/`access_token` and **discards `expires_in`**. No request-issuing method anywhere in `src/oci` retries or refreshes on 401.

The ordering makes this reachable in practice: `push.cpp:262` streams the multi-GB blob, then `push.cpp:280` issues `put_manifest` reusing the same cached token (likewise pull: long squashfs download, then meta `get_blob`). Any transfer outlasting the registry token TTL (JFrog default ~1 h; many registries 5–15 min) fails on the follow-up request **after the full transfer completed**. The old oras binary re-ran the token dance on every 401.

**Fix:** track `expires_in` and refresh proactively, and/or retry once with a fresh token on 401.

### 7. `uenv image copy` silently drops attached metadata and reports success

**Where:** `src/oci/push.cpp:413-420` (`copy_image`)

Any `src->referrers()` failure — including transient errors and 404 from registries without the API — is logged at `spdlog::debug` only and `copy_image` returns success. The image and blobs copy; the uenv/meta attachment does not. `copy_image` also never consults or recreates the `sha256-<hex>` referrers-tag fallback on the destination (referrer manifests are pushed by digest only, push.cpp:442), so even successfully copied metadata is undiscoverable on a destination lacking the Referrers API. The old `oras cp --recursive` copied attachments.

**Impact:** copy exits 0; a later pull of the destination errors "uenv exists in registry but has no attached metadata" — the deployed uenv loses its views/env.json.

**Fix:** surface referrers failure (warn at minimum; error for transient failures), and maintain the tag fallback on the destination.

---

## Medium

### 8. Wrong-typed registry JSON aborts the CLI via `std::terminate`

**Where:** `src/oci/manifest.cpp:133,137,139,164,170,195-204`; also `src/oci/client.cpp:111-117`

`json::parse` at manifest.cpp:164 uses `allow_exceptions=false`, but that only suppresses *parse* errors. The subsequent `.value("digest", ...)`, `.value("size", ...)`, `.value("mediaType", ...)` calls throw `nlohmann::json::type_error` (302) when a key exists with a mismatched type (e.g. `{"layers":[{"digest":123}]}` or `{"config":{"size":"2"}}`), and the layer loop at line 195 never checks `is_object()`, so `"layers":[1]` throws type_error.306. Nothing on the call path catches it (`cli/pull.cpp:302` catches only `util::signal_exception`; `main` has no try/catch; no `JSON_NOEXCEPTION` define) — the CLI aborts with "terminate called…" instead of returning a parse error.

Same hazard in `parse_referrers` (`client.cpp:111-117`). `src/oci/auth.cpp` (token response, docker config) shows the correct pattern: `find` + `is_string()`/`is_object()` before `get<>`.

**Fix:** apply the auth.cpp guard pattern (or a checked accessor helper) in `manifest.cpp` and `parse_referrers`.

### 9. Meta blob is used without digest verification (and double-buffered)

**Where:** `src/oci/pull.cpp:34-46,136`, `src/oci/client.cpp:245-266`

`pull_meta` fetches the meta tar.gz via the in-memory `get_blob`, which returns `resp->body` after only a status check — **no sha256 verification** — whereas the streaming `get_blob_to_file` hashes during download and rejects mismatches (client.cpp:297-327). `extract_targz` then writes the unbounded buffer back out to a temp file for tar, so the blob crosses RAM and disk twice.

**Impact:** the integrity gap is the important half — the extracted meta (env.json/views) is later sourced into user environments, and it is the only downloaded artifact that skips verification.

**Fix:** use `get_blob_to_file` straight to the temp path — fixes both the verification gap and the double buffering.

### 10. Docker credential helpers (`credsStore`/`credHelpers`) silently degrade to anonymous — regression vs oras

**Where:** `src/oci/auth.cpp:259-267` (`creds_from_docker_config`)

Only inline base64 `auth` entries are understood. A `docker login` performed with a credential store configured (the default with Docker Desktop / `secretservice` / `osxkeychain`) leaves an **empty** auths entry; the new code emits an `spdlog::warn` and returns `nullopt`, so `resolve_credentials` falls through to anonymous.

The old flow genuinely supported this: uenv invoked the `oras` binary with no credential flags and oras-go executed `docker-credential-*` helpers natively (main's `push.cpp:190` help text documented `~/.docker/config.json` as the alternative to `--token`).

**Impact:** users with helper-stored credentials regress to anonymous access and get 401/403 on restricted uenvs despite valid stored credentials.

**Fix:** exec the `docker-credential-<store>` helper (`get` protocol is a trivial stdin/stdout JSON exchange via `util::subprocess`), or at minimum promote the warning to a clear actionable error.

---

## Confirmed findings below the top-10 cap

### 11. Multiple `WWW-Authenticate` headers break challenge parsing (last-value-wins map)

**Where:** `src/util/curl.h:39`, `src/util/curl.cpp:29`, `src/oci/auth.cpp:97`, `src/oci/parse.cpp:379-410`

Response headers are stored in `std::unordered_map` with `entries[to_lower(name)] = value` — duplicates overwrite. A registry sending `Bearer` then `Basic` challenges (legal per RFC 7235; Harbor and nginx-fronted registries do this) leaves only `Basic`, which `parse_bearer_challenge` rejects ("expected a 'Bearer' authentication scheme"), so `client::create` fails and the registry is unusable. The single-header comma-separated form fails in **both** orderings (`Basic …, Bearer …` fails the scheme check; `Bearer …, Basic …` fails at "expected '=' after the parameter name"). Mitigating context: CSCS JFrog and zot send a single Bearer challenge, so this bites only third-party registries — but for those it is total.

**Fix:** accumulate duplicate headers (multimap or comma-append per RFC 7230) and select the Bearer challenge among multiple.

### 12. 401 errors lost their actionable guidance — users are told to file a service-desk ticket

**Where:** `src/util/curl.cpp:47-61` (`http_message`) vs deleted `src/uenv/oras.cpp` (`create_error`)

The deleted `create_error()` mapped auth failures to "Try using the --token flag if you are trying to access restricted software", token-revoked, and `--username` hints. The replacement maps only 403 and 408; a 401 on a manifest GET now surfaces as: *"failed to fetch manifest … (status 401): internal error contacting a network service - please create a CSCS service desk request…"*. That misdiagnoses a missing-credential error as a service outage; even the 403 mapping dropped the `--token` suggestion. Expect spurious service-desk tickets for restricted-software pulls.

**Fix:** restore a 401/403 mapping with the `--token` guidance at the point where registry responses are converted to user-facing errors.

### 13. `getlogin()` username-defaulting inside `src/oci` — fails in batch contexts, duplicated with diverging text

**Where:** `src/oci/auth.cpp:196,214`

The generic OCI library defaults the username from `getlogin()` and emits CLI-flag-specific error text. In a no-tty context (Slurm batch step) `getlogin()` returns NULL, so a push with `--token` but no `--username` fails there while working interactively; per POSIX it can also report the wrong user. The identical block appears twice (get_credentials:190-201 and creds_from_token:208-219) with already-diverged messages ("for the --token." vs "for the token."). Policy belongs in `src/cli/util.cpp` `resolve_registry_credentials`, which owns all environment-derived inputs — consistent with the CLAUDE.md "self-contained src/oci" and `envvars::state` philosophy.

### 14. `uenv image push` hashes the multi-GB squashfs twice

**Where:** `src/cli/push.cpp:129,185`, `src/cli/util.cpp:133`, `src/oci/push.cpp:45-46,247`

`validate_squashfs_image` computes `util::sha256_file` and stores it in `img.hash` — which is then never used; `oci::push_squashfs` takes only the path and re-hashes the same file via `digest_of_file`. For a 1–60 GB image that is one redundant full read pass plus sha256 (roughly 30 s to several minutes of dead time on Lustre before the upload starts, with the progress bar idle).

**Fix:** pass the validated digest into `push_squashfs` (optional precomputed-digest parameter).

### 15. Progress-bar block copy-pasted between pull and push

**Where:** `src/cli/pull.cpp:247-271`, `src/cli/push.cpp:154-184`

~20 effectively identical lines: atomic MB counter (with the same threading comment), round-up-to-MB formula, identical `bk::ProgressBar` options (`.speed = 0.1`, `.speed_unit = "MB/s"`, Rich/Bars via `color::use_color()`, `no_tty` from `isatty`), MB-conversion lambda, force-100%-then-`done()` pattern. Only the message, byte-count source, and success predicate differ. Incidentally, push's `file_size` fallback to 0 contradicts its own "never zero" comment. Extract a `make_transfer_bar(total_bytes, message)` helper into `src/cli/util.h` (must return a non-movable/heap handle since barkeep holds a pointer to the counter). The third bar in `repo.cpp:539` is configured differently and need not be unified.

### 16. `curl::upload()` not migrated to the new `perform()` primitive

**Where:** `src/util/curl.cpp:125-207`

The diff migrated `post()`, `get()`, and `del()` onto `perform()`, but `upload()` (sole caller: `src/cli/build.cpp:128`) keeps ~80 lines of hand-rolled `curl_easy_setopt` boilerplate duplicating errbuf/user-agent/timeout/write-callback setup. `perform()` already covers it: `request::upload_file` drives `CURLOPT_UPLOAD`/`READDATA`/`INFILESIZE_LARGE` (curl.cpp:378-388); the only unset options are `CURLOPT_USE_SSL` (no-op for https URLs) and forced HTTP/1.1. Fixes to `perform()` won't reach `upload()` until it is rewritten as a thin wrapper, as `del()` was.

---

## Cross-cutting recommendations

1. **Decide the listing-service digest contract first** (finding 1) — it determines whether pull addresses by tag or digest, and whether `listing_mock`'s contract is right.
2. **Introduce a status-carrying error type in `src/oci/client`** — findings 4, 5, 7, and 12 all stem from flat `std::string` errors that make 404 indistinguishable from transport failures and strip the HTTP status the caller needs.
3. **Unify referrers handling behind one read path** with the tag-schema fallback (findings 4, 7) so pull, copy, and attach share the same discovery logic the push side already maintains.
4. **Audit every download path for the "verify + clean up on failure" invariant** (findings 2, 9): stream to temp, hash while streaming, rename on success, remove on any failure.
