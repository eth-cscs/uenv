# TOML and multi-repo work

There are two tasks in this work

1. update uenv to use TOML for configuration files
2. add support for multiple repositories

- Perform task 1 and replicate the current mono-repo behavior and maintain backwards compatibility with existing inputs
- Then do task 2

Doing this in a single PR might be advisable, because at the end of Task 1 the input file format would not be the final format
- because it would represent repo as a scalar instead of a list
- we could make the toml format in step 1 a list, and simple pick the most recently defined version?

## multi repository policy

What behavior do we expect when querying multiple repositories

### single repo policy

The current single repo behavior should be reproduced in the new version when only one repository is provided

The query function returns all uenv that match.

Downstream consumers (ls, run, start, inspect, rm, push):
- `ls` shows all matches
- the others require that there is one and one match only because they imply an operation that uses a single uenv
    - if there are multiple matches, the user can refine the label to get an exact match

### multi repo policy

The `ls` scenario:

1. search all repos and concatenate results
2. apply a stable sort such that matches with the same label are in repo order

The unique repo scenario:

We might want two behaviors:

1. `run`, `start`, `inspect` are a little more forgiving: with a more greedy algorithm that will ignore ambiguous matches in lower-priority repos if finds a unique match in a higher-priority repo
- to avoid the same image in a user repo clashing with a site-managed repo
2. `rm` and `push` require
- an unambiguous match over all repos; or
- that a single repo is used (either `--repo` flag is used to constrain search, or the default list or repos is singular)

## work plan

### Step 1: replicate current behavior with toml

+ add toml library to the project
+ write toml spec for input files
+ create test input files and write unit tests
+ write toml config parser
    * do it alongside the existing parser or simply detect config exists but config.toml does not and print a warning
+ fall back to old config file if new one is not detected
    + generate a `config.toml` file to replace `config` for user configs

### Step 2: support multiple repositories

+ create repo description type `{name, path, priority}`
+ implement parsing of the type from toml
+ implement merging from defaults to CLI args
    + parsing --repo flag
    + sorting of repos
    + overwriting the final list based on repo
+ update CLI parsing to parse args like `--repo=$HOME/myrepo,site`
- store repos as a list e2e (with validated list of repos inside the global)
    - raise the search code to a single interface that is used by all sites (ls, run, start, etc)
    - initially simply search the first repo in the list to reproduce current behavior
- update `uenv repo` command to handle multi-repo case
- implement validation and initialization of input repos
    - this includes logic for creating default repo in a logical way
- implement multi-repo search
- update vservice to take an argument that points to a cluster-specific repo

## toml spec

```toml

color = boolean

[[repositories]]

name = string
path = string
priority = uint32

[elastic]

url = string
```

## search function

```cpp
// find a uenv based on description that might be a concrete squashfs path OR a label
// - run, start
resolve_uenv(string description, [repo_description] repos) -> [uenv_info]

// search for a label in a list of repos
// - return error if repos is empty: lower all that checking to the function, instead of replicating in all call sites
// - image ls
// - todo: uenv_label might need to be populated with system name _before_
resolve_uenv(uenv_label label, [repo_description] repos) -> [uenv_info]

// search an individual repo
// maybe this is not needed, if we can call the database routine below
resolve_uenv(uenv_label label, repo_description repo) -> [uenv_info]

// search through open database
// - image find
resolve_uenv(uenv_label label, database store) -> [uenv_info]
````

The current `resolve_uenv` function takes as its input a label and a repo path
- it 
