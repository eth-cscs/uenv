
The *manifest* for a uenv squashfs is a JSON file that can be obtained using `oras manifest fetch rego/tag`

* there is always the same empty `config` field
    * `e30=` is an empty JSON object `{}` in base64 encoding
* there is a single layer: the squashfs file
* there is a single annotation with the time of creation

```console
$ oras manifest fetch localhost:5862/deploy/cluster/zen3/app/1.0:v1 | jq .
{
  "schemaVersion": 2,
  "mediaType": "application/vnd.oci.image.manifest.v1+json",
  "artifactType": "application/x-squashfs",
  "config": {
    "mediaType": "application/vnd.oci.empty.v1+json",
    "digest": "sha256:44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a",
    "size": 2,
    "data": "e30="
  },
  "layers": [
    {
      "mediaType": "application/vnd.oci.image.layer.v1.tar",
      "digest": "sha256:4f9aa7ee3a5a056c3742119a69b2aa6eafefa46e571ce3f377f67aebdf43c2db",
      "size": 4096,
      "annotations": {
        "org.opencontainers.image.title": "app42.squashfs"
      }
    }
  ],
  "annotations": {
    "org.opencontainers.image.created": "2025-06-17T08:32:08Z"
  }
}

```

Every file (squashfs, tar ball of meta path, manifest.json, etc) in a registry has a *digest* of the form `sha256:<64 character sha>`.

* the config json has a digest that is `printf '{}' | sha256sum`
* the squashfs digest is the hash of the squashfs file

The manifest itself is referred to as

We can pull individual files/artifacts, aka blobs:

```console
$ oras blob fetch --descriptor \
    localhost:5862/deploy/cluster/zen3/app/1.0@sha256:4f9aa7ee3a5a056c3742119a69b2aa6eafefa46e571ce3f377f67aebdf43c2db
{
  "mediaType": "application/octet-stream",
  "digest": "sha256:4f9aa7ee3a5a056c3742119a69b2aa6eafefa46e571ce3f377f67aebdf43c2db",
  "size": 4096
}
$ oras blob fetch --output=store.squashfs \
    localhost:5862/deploy/cluster/zen3/app/1.0@sha256:4f9aa7ee3a5a056c3742119a69b2aa6eafefa46e571ce3f377f67aebdf43c2db
```

Note that `blob fetch --descriptor` returns file size, but not file name (`store.squashfs`)
* the file name is in `layers[0]['annotations']['org.opencontainers.image.title']`

## downloading squashfs

There are two methods: `oras blob fetch` and `oras pull`

```
TAG=v1
DIGEST=sha256:4f9aa7ee3a5a056c3742119a69b2aa6eafefa46e571ce3f377f67aebdf43c2db
URL=localhost:5862/deploy/cluster/zen3/app/1.0

# blob fetch uses the digest
oras blob fetch --output=store.squashfs $URL@$DIGEST

# pull uses the digest or tag
oras pull --output=store.squashfs $URL:$TAG
oras pull --output=store.squashfs $URL@$DIGEST
```

## downloading meta path

There is one way: use `oras pull`

```console
$ TAG=v1
$ URL=localhost:5862/deploy/cluster/zen3/app/1.0

$ oras discover --format=json --artifact-type=uenv/meta localhost:5862/deploy/cluster/zen3/app/1.0:v1 | jq -r '.manifests[0].digest'
sha256:d5d9a6eb9eeb83efffe36f197321cd4b621b2189f066dd7a4b7e9a9e6c61df37

$ DIGEST=sha256:d5d9a6eb9eeb83efffe36f197321cd4b621b2189f066dd7a4b7e9a9e6c61df37
$ oras pull $URL@$DIGEST

# using image blob fetch downloads something, but I don't know how to turn it into the meta path
oras blob fetch --output meta $URL@$DIGEST
```

# Abstraction

Requirements:

* squashfs: size, sha,
* url
* meta: sha


```cpp
struct manifest {
    sha256 digest;
    sha256 squashfs_digest;
    size_t squashfs_bytes;
    optional<sha256> meta_digest;
    std::string respository;
    std::string tag;
}
```


Before we used the `uenv-list` service to generate a full database, which we could then search on

wait now, a manifest is
* a json file
* a description using OCI format of layers and meta

```
// complete all information
// - sha of squashfs, sha of meta, full url
manifest fetch_manifest(url, record) {
    tag_url = url + record;
    mf = oras manifest fetch tag_url
    meta = oras discover tag_url
    digest = oras resolve tag_url
    manifest M {
        .digest = ??;
        .squashfs = mf[layers][0][digest]
        .squashfs_bytes = mf[layers][0][size]
        .meta = meta[manifests][0].digest;
        .repository = url + record (no tag/digets);
    }

}

pull_digest(url, sha, path) {
    
}

```

uenv image push
    TODO check for existing image
    oras push --artifact-type=application/x-squashfs squashfs tag
    oras attach --artifact-type=uenv/meta tag meta_path

uenv image pull
    TODO check for existing image
    oras pull --output=path/store.squashfs manifest.repository@manifest.squashfs_digest
    oras pull --output=path/store.squashfs manifest.repository@manifest.meta_digest

uenv image delete
    TODO check for existing image
    curl -X Delete ...

uenv image copy

# WORKFLOW

If the rego.supports_search():
    vector<records> registry.match(input_label);
    if there is one match then set record
If record is unique
    pull
    delete
    copy


```
struct registry
    util::expected<uenv::repository, std::string>
    listing(const std::string& nspace) const;
    url() const;
    supports_search() const;
    type() const;
    manifest(label)
```

