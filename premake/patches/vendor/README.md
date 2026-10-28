# Funke vendor patches

These patches reproduce the small Funke-owned changes made to pinned upstream
submodules. Normal Premake generation runs `scripts/vendor_patches.py apply`
before defining projects. The command is idempotent and rejects an unexpected
submodule commit, file path, blob ID, staged change, or untracked file.

CI first runs the non-mutating `verify` mode against a fresh checkout and checks
the applied blob IDs again after Premake generation.

The EnTT patch includes the configurable entity/version bit split, staged bulk
storage operations for trusted parallel ECB adapters, and exact sole-listener
inspection. Bulk storage bypasses lifecycle signals; callers certify the hooks
and join all nonthrowing payload work before exposing the storage again.

When upgrading a patched submodule:

1. Update the submodule pin and port the existing change locally.
2. Regenerate its full-index unified diff with
   `git -C External/<name> diff --full-index --binary`.
3. Update the pin plus base/patched blob IDs in `scripts/vendor_patches.py`.
4. Run `python scripts/vendor_patches.py verify --require-applied` and a normal
   Premake generation.
