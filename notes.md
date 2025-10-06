# LFS notes

source code for lustre functionality is in `src/util/lustre.[cpp,h]`

## API

need to query
+ striping of a path
+ whether a file/path is in a lustre file system
    - `stat -f -c %t $file == bd00bd0`
+ whether lfs is present
    - `which lfs`

need to set
- apply striping to a path
- change striping of a file
    - `lfs migrate`

## Where will this be applied

A generic instance

```
uenv repo update
```

when a repo is created

```
uenv image pull
uenv image push
uenv image find
```

where is this implemented?

```
uenv::create_repository(settings.config.repo.value());
```
