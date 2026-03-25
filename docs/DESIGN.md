# Mini-UnionFS Design Document

## 1. Introduction

Mini-UnionFS is a simplified Union File System that runs in userspace using FUSE (Filesystem in Userspace). It demonstrates the core concepts behind Docker's layered filesystem architecture by merging multiple directories (layers) and implementing Copy-on-Write (CoW) semantics.

## 2. Architecture Overview

### 2.1 System Design

The system consists of three main layers:

1. **Linux Kernel** - Intercepts filesystem calls through FUSE kernel module
2. **FUSE Daemon** - Our userspace program (mini_unionfs.c) handling filesystem operations
3. **Underlying Filesystem** - Two directory trees (lower_dir and upper_dir)

```
User Application
       ↓ (read/write calls)
Linux Kernel (VFS)
       ↓ (FUSE protocol)
Mini-UnionFS Daemon
       ↓ (file operations)
Lower Dir (read-only)  +  Upper Dir (read-write)
       ↓
Actual Filesystem
```

### 2.2 Key Components

#### Global State Structure
```c
struct mini_unionfs_state {
    char *lower_dir;  // Read-only base image layer
    char *upper_dir;  // Read-write container layer
};
```

This state is passed to all FUSE callbacks through `fuse_get_context()->private_data`.

#### Path Resolution
The `resolve_path()` function implements the core layer stacking logic:

1. **Whiteout Check**: If `.wh.<filename>` exists in upper_dir, the file is deleted (return -ENOENT)
2. **Upper Precedence**: If file exists in upper_dir, use it (layer = 1)
3. **Lower Fallback**: If file exists only in lower_dir, use it (layer = 2)
4. **Not Found**: If file doesn't exist anywhere, return error

This ensures upper layer files take precedence, and whiteout files hide lower layer files.

## 3. Core Features Implementation

### 3.1 Copy-on-Write (CoW)

**Mechanism**: When a process opens a file from lower_dir for writing, we automatically copy it to upper_dir before allowing the write.

**Implementation**:
- Triggered in `unionfs_open()` when O_WRONLY, O_RDWR, or O_APPEND flags are set
- Also triggered in `unionfs_write()` for consistency
- Uses `copy_file()` helper which:
  - Preserves original file permissions and metadata
  - Reads source file in 4KB chunks
  - Writes to destination in upper_dir
  - Cleans up on error

**Benefits**:
- Lower layer remains pristine
- Multiple readers can share same lower file
- Writers get isolated copies
- Efficient space usage (files shared until modified)

### 3.2 Whiteout Mechanism

**Concept**: A whiteout file is a special marker file named `.wh.<original_filename>` that signals deletion.

**Implementation**:
- When user deletes a file from lower_dir via the union mount:
  - `unionfs_unlink()` creates `.wh.filename` in upper_dir
  - This is an empty marker file
  - The actual lower_dir file remains unchanged

**Resolution in `resolve_path()`**:
```
If (.wh.config.txt exists in upper)
    → Return -ENOENT (file appears deleted)
    → Hidden from readdir listings
```

**Edge Cases Handled**:
- Whiteout files are never returned in directory listings
- Multiple whiteout files don't conflict
- Creating a file with same name as whiteouted file replaces the whiteout

### 3.3 Layer Merging in readdir

The `unionfs_readdir()` function implements merge logic:

1. List all entries from upper_dir
2. Explicitly skip whiteout files (`.wh.*`)
3. List remaining entries from lower_dir
4. Avoid duplicates (if file exists in both, show only once from upper)
5. Filter out whiteouted entries from lower_dir

This provides a seamless merged view to applications.

## 4. FUSE Operations Implemented

| Operation | Behavior |
|-----------|----------|
| **getattr** | Resolve path and return stat info from actual file |
| **open** | Trigger CoW if writing to lower_dir file |
| **read** | Resolve path and read from actual location |
| **write** | Trigger CoW if needed, write to upper_dir location |
| **readdir** | Merge listings from both layers with whiteout filtering |
| **create** | Create file in upper_dir |
| **unlink** | Delete from upper (if there) or create whiteout (if from lower) |
| **mkdir** | Create directory in upper_dir |
| **rmdir** | Similar logic to unlink for directories |
| **rename** | Copy/move from lower if needed, then rename in upper |
| **truncate** | Trigger CoW if file is in lower_dir first |
| **release** | Cleanup on file close (no special handling needed) |

## 5. Data Structures

### Path Buffers
- All path operations use fixed 4096-byte buffers
- Constructed as: `<base_dir>/<relative_path>`
- Example: `/tmp/upper/ + /file.txt` → `/tmp/upper//file.txt`

### File Descriptor Management
- Original file descriptors from lower_dir are not cached
- Each operation opens, uses, and closes file descriptors
- This avoids state management complexity

### Directory Entries
- Uses system `struct dirent` from `<dirent.h>`
- Relies on OS-level directory reading

## 6. Edge Cases & Resolution

### 6.1 Concurrent Access
**Issue**: Multiple processes accessing same file
**Solution**: FUSE kernel module serializes requests by default; each request runs independently with path resolution at call time

### 6.2 Parent Directory Missing in Upper
**Issue**: Creating `/a/b/c.txt` when `/a/b/` doesn't exist in upper_dir
**Solution**: `ensure_parent_dir()` recursively creates missing parent directories with 0755 permissions

### 6.3 File Exists in Both Layers
**Issue**: Same filename in both lower and upper
**Solution**: `resolve_path()` always returns upper_dir version first (precedence)

### 6.4 Deleting from Lower Then Recreating
**Sequence**: 
1. User deletes `/file.txt` (from lower) → creates `.wh.file.txt` in upper
2. User creates new `/file.txt` → writes to upper (whiteout file left behind unmodified)

**Solution**: When creating, we don't explicitly remove whiteout file; the new file in upper takes precedence

### 6.5 Deleting Non-Existent File
**Issue**: `rm` on unmounted file
**Solution**: `resolve_path()` returns -ENOENT; `unlink()` propagates this error

### 6.6 Writing to New File in Upper
**Issue**: File doesn't exist anywhere
**Solution**: `write()` detects error from `resolve_path()` and creates new file in upper_dir with O_CREAT flag

## 7. Error Handling

All operations return standard POSIX error codes:
- **-ENOENT**: File not found (or whiteouted)
- **-EEXIST**: File already exists
- **-EACCES**: Permission denied
- **-EISDIR**: Is a directory
- **-ENOTDIR**: Not a directory
- **-EIO**: I/O error during copy
- **-EINVAL**: Invalid argument

## 8. Performance Considerations

### Optimization Done
- Lazy copying (CoW) avoids upfront full filesystem copy
- Whiteout files are empty (negligible space overhead)
- Directory merging happens on-demand

### Potential Bottlenecks
- First write to large file triggers full file copy (unavoidable for correctness)
- Path resolution does multiple `lstat()` calls per operation (could cache)
- No inode tracking (each stat is independent)

## 9. Testing Strategy

The `test_unionfs.sh` script validates:

1. **Layer Visibility** - Files from both layers are visible
2. **Upper Precedence** - Same filename in both shows upper version
3. **Directory Merging** - Unified directory listing works
4. **Copy-on-Write** - Writes to lower files create upper copies
5. **Whiteout** - Deletions create `.wh.` files and hide lower files
6. **File Creation** - New files created in upper_dir
7. **Nested Directories** - Directories and their contents accessible

## 10. Limitations & Future Work

### Current Limitations
- No support for symbolic links
- No support for special files (sockets, devices)
- No file locking mechanisms
- No extended attributes
- Simple path resolution (no caching)

### Potential Enhancements
1. **Performance**: Implement inode-level caching to reduce lstat() calls
2. **Features**: Add support for POSIX permissions and ownership
3. **Robustness**: Add filesystem consistency checks on mount
4. **Diagnostics**: Add verbose logging mode for debugging

## 11. Compilation & Deployment

### Build Requirements
- GCC/Clang compiler
- FUSE 3.x development libraries (`libfuse-dev`)
- POSIX-compliant Linux environment

### Build Command
```bash
make clean && make
```

### Execution
```bash
./mini_unionfs <lower_dir> <upper_dir> <mount_point>
```

### Unmounting
```bash
fusermount -u <mount_point>    # Linux
umount <mount_point>            # macOS/BSD
```

## 12. Conclusion

Mini-UnionFS successfully demonstrates the core mechanisms of Docker's layered filesystem architecture. The implementation prioritizes correctness and clarity over performance, making it suitable for educational purposes. The Copy-on-Write semantic provides efficient multi-layer file sharing, while the whiteout mechanism elegantly handles deletions across layers.
