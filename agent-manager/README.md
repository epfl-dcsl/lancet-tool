# Building the binary assets

The binary assets need to build at a specific path.
Relative to this folder do the following

```shell
mkdir manager/assets
cd manager/assets
cmake ../../../ # using additional flags as you'd like
make # or ninja
```

Without this, the `setup.py` script fails to avoid silently missing these files.
