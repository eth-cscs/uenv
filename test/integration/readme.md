# Integration Tests

Integration test for the CLI and SLURM plugin, using the [bats](https://github.com/bats-core/bats-core) testing framework.

## getting started

Before running tests, first download bats:

```console
./install-bats
```

## running the tests

There are two separate sets of tests.

- `slurm.bats`: test the slurm plugin
    - ensure that you have built the plugin and configured slurm in your environment to use the plugin.
- `cli.bats`: test the uenv cli
    - ensure that you have built the cli and that it is in your `PATH`.

To run the respective tests, use the copy of bats installed in this path using the `./install-bats` script above:
```console
./bats ./slurm.bats
./bats ./cli.bats
```
