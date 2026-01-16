# TOML and multi-repo work

There are two tasks in this work

1. update uenv to use TOML for configuration files
2. add support for multiple repositories

- Perform task 1 and replicate the current mono-repo behavior and maintain backwards compatibility with existing inputs
- Then do task 2

Doing this in a single PR might be advisable, because at the end of Task 1 the input file format would not be the final format
- because it would represent repo as a scalar instead of a list
- we could make the toml format in step 1 a list, and simple pick the most recently defined version?

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

- create repo description type `{name, path, priority}`
- implement parsing of the type from toml
- implement merging from defaults to CLI args
- implement validation and initialization of input repos
- update `uenv repo` command to handle multi-repo case
- search repos in order
    - raise this to a single interface used by all methods that search/find in a repo
- update vservice to take an argument that points to a cluster-specific repo
- update CLI parsing to parse args like `--repo=$HOME/myrepo,site`

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
