How to "migrate" a repository?

The vast majority of users use the default repository location:

default=$SCRATCH/.uenv-images

Migrating would require:

- create repository in new location `$SCRATCH_NEW/.uenv`
- transfer images and database from the old location
- set the new default location 
    - update the user's `.config/uenv/config`

What about a new user who logs in for the first time with no defaults?
- a repository will be created at the "old" default

Also have to handle in the process: migration from v5 databases (don't just copy)

Selection of the default location is the tricky part

```
defaults=[ /ritom/scratch/cscs/$USER/.uenv, /capstor/scratch/cscs/$USER/.uenv-images, /capstor/scratch/cscs/$USER/.uenv-images ]

repo = null
for p in defaults:
    if is_repo(p):
        repo=p
        break
if !repo:
    error

use repo
```

### status

debugging a final issue.

`uenv repo migrat --sync` is not copying a uenv `prgenv-gnu`

```
$ ./uenv image ls prgenv-gnu
uenv                  arch   system  id                size(MB)  date
prgenv-gnu/25.06:rc3  gh200  daint   dc97bbdd859092e4   5,455    2025-06-06
prgenv-gnu/25.6:v2    zen2   eiger   724fb61dbdb04006     592    2025-09-15

$ ./uenv --repo=$HOME/tmp/.uenv image ls prgenv-gnu
uenv                  arch   system  id                size(MB)  date
prgenv-gnu/25.06:rc3  gh200  daint   dc97bbdd859092e4   5,455    2025-06-06

$ ./uenv repo migrate --sync $HOME/tmp/.uenv
migrate repo from /home/bcumming/.uenv/repo to /home/bcumming/tmp/.uenv
migration finished sucessfully
```

observation:
- the missing uenv path for `724fb61dbdb04006` is not copied
- the logs say that the record for the uenv is added
- but `image ls` draws a big blank on the destination repo

