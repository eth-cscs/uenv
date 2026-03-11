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


- `resolve_uenv` was designed to return a single "resolved" uenv (zero or more than one match is an error)
    - it is used in contexts that want a single uenv (e.g. run, start, inspect)
    - I have adapted various backend implementations to instead return a vector of uenv
- so `resolve_uenv` should be a wrapper function that calls the more generic "search" function that returns a vector, then applies its own logic to extract a single uenv
    - e.g. 3 matches, with two matches from the first repo -> error
    - e.g. 3 matches, with one match   from the first repo -> all good


```cpp
//
// impl:: level: low level that should only be called by back end code
//

// fill in a complete uenv_info for a uenv in an on-disk store
resolve_uenv_info(uenv_record, repo_description, database) -> uenv_info

// fill in a complete uenv_info for a uenv from a direct file reference
resolve_uenv_info(path) -> uenv_info

find_uenv(uenv_label label, repo_description repo) -> [uenv_info]
{
    [uenv_info] out
    store = open(r)
    for (result: store.find(label)) {
        out.append(resolve_uenv_info(result.record, r, store);
    }
    return out;
}

// search for a label in a list of repos
// - image ls
// - todo: uenv_label must be populated with system name _before_
find_uenv(uenv_label label, [repo_description] repos) -> [uenv_info]
{
    [uenv_info] out
    for (r: repo) {
        store = open(r)
        for (result: store.find(label)) {
            out.append(resolve_uenv_info(result.record, r, store);
        }
    }
    return out;
}

// THE BIG QUESTION: does find_uenv take a list of repos, or a single repo, and the iteration is handled one level up
// - when a single result is returned we would like to retain meta data about which repo it was found in
//      - the iteration one level up would be tempted to drop
// - 

// find a unique uenv based on description that might be a concrete squashfs path OR a label
// - run, start
resolve_uenv(string description, [repo_description] repos) -> uenv_info
{
    if (description as label) {
        result = find_uenv(label, repos);
        // post process result to look for a unique uenv

        // OR

        for (r: repos) {
            result = find_uenv(label, r);
            if (!r.size==1) {
                return error(no unique value)
            }
            return r[0];
        }
    }
    else if (description as path) {
        return resolve_uenv_info(path);
    }
}

// create concrete description of a complete uenv environment
// - checks mount points are valid and exist
// - loads environment patches
// - output is validated and coherent set of state ready to mount
// this takes a list of uenv_info that have already been checked
// - departure from the current implementation which consumes CLI arguments and calls realise_uenv
concretise_uenv([uenv_info] uenvs, views) -> env
{
    for (e: uenvs) {
        // assemble env
    }
}

// search an individual repo
// maybe this is not needed, if we can call the database routine below
resolve_uenv(uenv_label label, repo_description repo) -> [uenv_info]

// search through open database
// - image find
resolve_uenv(uenv_label label, database store) -> [uenv_info]
```

entry points

```
// search functionality
find_uenv(uenv_label, [repo_description]) -> [uenv_info]

resolve_uenv(uenv_label, [repo_description]) -> uenv_info
resolve_uenv(path) -> uenv_info
```


the `[uenv_info]` list of uenv is contains a natural partition that corresponds to uenv found in different repositories
```
[
    {prgen-env/24.11@daint, repo=user}
    {prgen-env/25.11@eiger, repo=user}
    {prgen-env/24.11@santis, repo=site}
]
```

## organising results

`std::vector<std::<uenv_info>>` is not a great container


```
struct uenv_list {
    using store_ = std::vector<std::<uenv_info>>;

    struct range {
        store_::const_iterator begin;
        store_::const_iterator end;
        uenv_description description;
        std::optional<std::string> error;
    };

    store_ values;
    std::vector<range_> repos;
};
```
