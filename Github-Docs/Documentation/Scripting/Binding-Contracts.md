# Interop declaration contracts

Native and managed bindings remain positional function-pointer tables. Layout
version and table sizes are checked at startup; a signature or slot-order change
requires coordinated definitions, a layout-version bump and rebuilding both sides.
The Physics3D query additions use layout version 56; they append slots and retain
all earlier slot indices. Restart the editor after rebuilding the engine and
ScriptCore together. Native-script ABI 8 includes the per-attachment Bolt state in ScriptInstance. Rebuild existing native script libraries against the matching engine headers.

The bounded pilot area is the 16 Physics3D slots in
`scripts/ci/contracts/physics3d-bindings.json`. Each declaration records its absolute
index and exact C++/C# signature. Ownership is synchronous: output pointers belong
to the caller and are never retained. The ordinary platform calling convention is
the existing `delegate* unmanaged` contract; no platform-specific layout is added.

`python scripts/ci/validate_binding_contract.py` checks both source tables against
that common declaration, module-slot membership and matching layout versions.
It validates source rather than generating runtime code and adds no runtime cost.
The five `test_binding_contract.py` cases run through existing CI unittest
`test_*.py` discovery: current definitions, a one-sided signature mutation,
equal-sized slot reordering, a missing disabled-module entry and mismatched
versions. The existing runtime handshake tests retain version/size rejection.

The broader tables are still handwritten. `ScriptBindingModuleSlots.hpp` supplies
module assignment/stub membership; it does not generate all native/managed type
signatures. This contract does not claim coverage of every binding or replace
real module-disabled/runtime startup tests.
