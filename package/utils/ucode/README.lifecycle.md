# VM resource lifetime

Resource type prototypes created by `uc_type_declare()` participate in the VM
value pool. The type's owning reference is released before final garbage
collection, so prototype properties and cycles are destroyed while referenced
VM values still exist. The resource
type descriptors remain allocated until collection has run every resource
destructor.

The backport changes the inline `uc_type_declare()` helper. Rebuild modules and
embedding applications against the updated header together with libucode, then
restart processes which already loaded the old library or modules. The binary
ABI and library soname are unchanged.

With `UNIT_TESTING=ON`, the upstream CTest suite includes
`vm-resource-prototype`. It exercises VM teardown with prototype-held objects,
arrays, prototype cycles, and a live native resource destructor. Run an
instrumented host build to check allocation lifetime as well as the destructor
count. The test is not installed on the target.
