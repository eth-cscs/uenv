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

TODO:

- update calculation of the default repo
- add hint to uenv repo migrate that shows how to update config file

