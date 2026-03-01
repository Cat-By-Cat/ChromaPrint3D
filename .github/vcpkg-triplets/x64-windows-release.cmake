set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Build and install release-only binaries to avoid dbg+rel dual builds in CI.
set(VCPKG_BUILD_TYPE release)
