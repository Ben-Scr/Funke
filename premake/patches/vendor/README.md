# Index vendor patches

These patches reproduce the small Index-owned changes made to pinned upstream
submodules. Normal Premake generation runs `scripts/vendor_patches.py apply`
before defining projects. The command is idempotent and rejects an unexpected
submodule commit, file path, blob ID, staged change, or untracked file.

CI first runs the non-mutating `verify` mode against a fresh checkout and checks
the applied blob IDs again after Premake generation.

When upgrading a patched submodule:

1. Update the submodule pin and port the existing change locally.
2. Regenerate its full-index unified diff with
   `git -C External/<name> diff --full-index --binary`.
3. Update the pin plus base/patched blob IDs in `scripts/vendor_patches.py`.
4. Run `python scripts/vendor_patches.py verify --require-applied` and a normal
   Premake generation.
