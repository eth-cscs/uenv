# Registry Abstraction Implementation Status

## Context
Refactoring hardcoded site-specific JFrog registry to support multiple registry types (OCI, Zot, GHCR, site-specific). Goal is to allow users to configure different registries while maintaining backward compatibility.

## Key Understanding
- **Repository**: Local disk storage (SQLite + subdirectories) for downloaded images
- **Registry**: Remote OCI container registry (JFrog, GHCR, Docker Hub, etc.)
- **Repository data structure**: Also used as in-memory container for registry search results (consistent query interface)

## Completed Work ✅

### 1. Core Abstraction (`src/uenv/registry.h` + `src/uenv/registry.cpp`)
- Created `registry_backend` abstract base class
- Implemented `registry_type` enum (oci, zot, ghcr, site)
- Added factory function `create_registry(url, type)`
- OCI/Zot/GHCR implementations return empty repositories (no search support)
- Added `supports_search()` capability flag

### 2. Site Integration (`src/site/site.h` + `src/site/site.cpp`)
- Added `jfrog_registry` class wrapping existing `registry_listing()` function
- Added `create_site_registry()` factory function
- Preserves existing JFrog custom API behavior

### 3. Configuration (`src/uenv/settings.h` + `src/uenv/settings.cpp`)
- Added `registry_type` field to `config_base` and `configuration`
- Added parsing for registry_type config parameter
- Default type is "site" for backward compatibility

### 4. CLI Utilities (`src/cli/util.h` + `src/cli/util.cpp`)
- Added `create_registry_from_config()` helper function
- Handles registry backend creation based on configuration

### 5. Build System (`meson.build`)
- Added `src/uenv/registry.cpp` to lib_src

### 6. Complete CLI Updates ✅
- Updated `src/cli/copy.cpp` to use new registry abstraction
- Updated `src/cli/delete.cpp` to use registry abstraction with graceful degradation  
- Updated `src/cli/pull.cpp` to use registry abstraction with graceful degradation
- Updated `src/cli/push.cpp` to use registry abstraction with graceful degradation
- ✅ All CLI files now use the registry abstraction
- ✅ Code compiles and tests pass

### 7. Type Erasure Refactoring ✅ 
- Refactored registry from abstract base class to type-erased value class
- Used concept-based approach similar to `help::item` pattern
- Registry implementations are now structs that satisfy `RegistryImpl` concept
- Registry objects can be stored by value, copied, and moved
- Updated all CLI utilities to use value-based API instead of pointer-based
- ✅ All code compiles and tests pass

## Remaining Work 🚧

### 2. Implement Graceful Degradation Pattern ✅
All CLI commands now implement graceful degradation:
- **delete.cpp**: Requires search support, fails gracefully if not available
- **pull.cpp**: Constructs minimal record for non-searchable registries
- **push.cpp**: Skips existence check for non-searchable registries with warning
- **copy.cpp**: Uses search for validation when available

Pattern implemented:
```cpp
auto registry_backend = create_registry_from_config(settings.config);
if (registry_backend->supports_search()) {
    // Existing logic: validate via listing first
    auto listing = registry_backend->get_listing(nspace);
    // ... validation, disambiguation, etc.
} else {
    // Graceful degradation: proceed without validation
    spdlog::info("Registry does not support search, proceeding without pre-validation");
    // ... direct ORAS operations with proper error handling
}
```

### 3. Enhance ORAS Error Handling
Ensure ORAS wrapper functions return well-structured errors:
- "Image not found" errors
- "Network/registry unreachable" errors  
- "Authentication failed" errors
- "Invalid image format" errors

### 4. Update Error Messages
- With search: "No image found matching 'X' in registry listing"
- Without search: "Failed to pull image 'X': image not found in registry"

### 5. Testing
- Test searchable registry (JFrog): Existing behavior preserved
- Test non-searchable registry: Graceful operation with proper errors

## Configuration Examples
```
# Site-specific (current default)
registry = jfrog.svc.cscs.ch/uenv
registry_type = site

# GitHub Container Registry
registry = ghcr.io
registry_type = ghcr

# Generic OCI registry
registry = docker.io
registry_type = oci
```

## Next Steps
1. Complete CLI file updates with supports_search() branching
2. Enhance ORAS error handling
3. Test with different registry types
4. Update documentation