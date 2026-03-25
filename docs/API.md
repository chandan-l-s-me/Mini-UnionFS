# Mini-UnionFS API Reference

## Core Data Structures

### mini_unionfs_state
```c
struct mini_unionfs_state {
    char *lower_dir;  // Read-only base image directory
    char *upper_dir;  // Read-write container directory
};
```

Accessed in FUSE callbacks via:
```c
struct mini_unionfs_state *data = UNIONFS_DATA;
```

## Path Resolution

### resolve_path(const char *path, char *resolved_path, int *layer)

Determines which layer a file exists in and resolves its actual filesystem path.

**Parameters:**
- `path`: Virtual path (e.g., `/file.txt`)
- `resolved_path`: Output buffer for actual filesystem path
- `layer`: Output parameter (1=upper, 2=lower, 0=not found)

**Returns:**
- `0`: Success
- `-ENOENT`: File not found or whiteouted
- Other: Standard errno values

**Example:**
```c
char real_path[MAX_PATH_LEN];
int layer;
int ret = resolve_path("/myfile.txt", real_path, &layer);

if (ret == 0) {
    if (layer == 1) {
        printf("File in upper layer: %s\n", real_path);
    } else {
        printf("File in lower layer: %s\n", real_path);
    }
}
```

**Algorithm:**
1. Check if `.wh.<filename>` exists in upper_dir → -ENOENT (whiteouted)
2. Check if file exists in upper_dir → Return upper path
3. Check if file exists in lower_dir → Return lower path
4. File not found → -ENOENT

## Helper Functions

### ensure_parent_dir(const char *path)

Recursively creates parent directories in upper_dir if they don't exist.

**Parameters:**
- `path`: Virtual path (e.g., `/dir1/dir2/file.txt`)

**Returns:**
- `0`: Success or already exists
- `< 0`: errno value

**Example:**
```c
ensure_parent_dir("/a/b/c.txt");
// Creates /upper_dir/a/ and /upper_dir/a/b/ if needed
```

### copy_file(const char *src, const char *dest)

Copy a file from source to destination, preserving file permissions.

**Parameters:**
- `src`: Source file path
- `dest`: Destination file path

**Returns:**
- `0`: Success
- `< 0`: errno value

**Notes:**
- Copies in 4KB chunks (efficient for large files)
- Preserves file mode/permissions
- Destination parent directories must exist
- Cleans up destination on failure

**Example:**
```c
// Copy-on-Write: copy lower file to upper before modifying
copy_file(lower_path, upper_path);
```

### is_whiteouted(const char *path)

Check if a file is whiteouted (marked for deletion).

**Parameters:**
- `path`: Virtual path to check

**Returns:**
- `1`: File is whiteouted
- `0`: File is not whiteouted

**Example:**
```c
if (is_whiteouted("/deleted_file.txt")) {
    // File appears deleted to user, even if exists in lower layer
}
```

## FUSE Operations

All FUSE operations follow the pattern:

```c
static int unionfs_OPERATION(parameters...) {
    // 1. Resolve path using resolve_path()
    // 2. Handle Copy-on-Write or whiteout if needed
    // 3. Call system function on resolved path
    // 4. Return result (0 for success, -errno for errors)
}
```

### getattr()
Get file metadata (stat information).

**Usage:**
```c
struct stat stbuf;
unionfs_getattr("/file.txt", &stbuf, NULL);
// stbuf now contains: size, permissions, timestamps, etc.
```

### open()
Open a file. Triggers Copy-on-Write if writing to lower_dir file.

**Parameters from fi (file_info):**
- `fi->flags`: Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT)

**Behavior:**
- `O_RDONLY`: No CoW needed
- `O_WRONLY, O_RDWR, O_APPEND`: Trigger CoW if file in lower_dir

**Example:**
```c
// User opens /file.txt for writing
// File exists only in lower_dir
// → open() automatically copies to upper_dir first
```

### read()
Read data from file.

**Parameters:**
- `buf`: Output buffer
- `size`: Number of bytes to read
- `offset`: Starting position in file

**Returns:** Number of bytes read, or < 0 for error

### write()
Write data to file. Triggers Copy-on-Write if needed.

**Parameters:**
- `buf`: Data to write
- `size`: Number of bytes
- `offset`: Starting position

**Returns:** Number of bytes written, or < 0 for error

**CoW Logic:**
```
If file doesn't exist anywhere:
    Create in upper_dir with O_CREAT
Else if file in lower_dir:
    Copy to upper_dir first (CoW)
    Then write to upper_dir
Else (file in upper_dir):
    Write directly to upper_dir
```

### readdir()
List directory contents. Merges listings from both layers.

**Parameters:**
- `buf`: Directory listing buffer
- `filler`: Function to add entries: `filler(buf, name, NULL, 0)`

**Behavior:**
1. List all entries from upper_dir
2. Skip whiteout files (`.wh.*`)
3. Add remaining entries from lower_dir (avoiding duplicates)
4. Skip whiteouted entries from lower_dir

**Example:**
```
lower contains: [a.txt, b.txt, c.txt]
upper contains: [b.txt (modified), d.txt]
whiteout:       [.wh.c.txt]

readdir result: [a.txt, b.txt (upper version), d.txt]
// c.txt hidden by whiteout, b.txt from upper (newer)
```

### create()
Create a new file.

**Parameters:**
- `mode`: File permissions (e.g., 0644)

**Behavior:**
- Always creates in upper_dir
- Automatically creates parent directories
- Fails if file already exists (O_EXCL)

### unlink()
Delete a file.

**Behavior:**
```
If file in upper_dir only:
    Delete directly from upper_dir

Else if file in lower_dir (or in both):
    Don't delete from lower
    Create .wh.<filename> in upper_dir as marker
    // File is now hidden from all operations
```

**Example:**
```
User: rm /mounted/base.txt    (base.txt is in lower_dir)
Result: /upper_dir/.wh.base.txt is created (empty marker file)
Effect: /mounted/base.txt appears deleted
But:    /lower_dir/base.txt remains intact
```

### mkdir()
Create a directory.

**Parameters:**
- `mode`: Directory permissions (e.g., 0755)

**Behavior:**
- Creates in upper_dir
- Automatically creates parent directories
- Fails if already exists

### rmdir()
Remove a directory.

**Behavior:**
- Directory must be empty
- Same logic as unlink for lower_dir (creates whiteout)

### rename()
Move/rename a file.

**Parameters:**
- `from`: Current path
- `to`: New path

**Behavior:**
```
Resolve source file:
If file in lower_dir:
    Copy to upper_dir at target path first (CoW)
Else (in upper):
    Rename directly in upper_dir

Optional: Create whiteout for old location if needed
```

### truncate()
Truncate file to specific size.

**Parameters:**
- `size`: New file size

**Behavior:**
- Triggers CoW if file in lower_dir
- Truncates in upper_dir (or created location)

## Error Codes

Standard POSIX error codes:

| Code | Meaning | Returned When |
|------|---------|---------------|
| 0 | Success | Operation completed successfully |
| -ENOENT | No such file/directory | Path doesn't exist or whiteouted |
| -EEXIST | File exists | Creating file/dir that already exists |
| -EACCES | Permission denied | No read/write permission |
| -EISDIR | Is a directory | Operation expects file, got dir |
| -ENOTDIR | Not a directory | Operation expects dir, got file |
| -EBUSY | Device/resource busy | Mount point in use |
| -EIO | I/O error | Error during file copy or read/write |
| -EINVAL | Invalid argument | Bad parameters |

## Whiteout Files

### Creation
```c
// When user deletes /config.txt from lower_dir
char whiteout_path[MAX_PATH_LEN];
snprintf(whiteout_path, MAX_PATH_LEN, "%s/.wh.config.txt", upper_dir);
int fd = open(whiteout_path, O_WRONLY | O_CREAT, 0644);
close(fd);
// File is now created as empty marker
```

### Resolution
```c
// In resolve_path()
if (lstat(whiteout_path, &st) == 0) {
    // Whiteout exists - return ENOENT
    return -ENOENT;
}
```

### Important Properties
- Empty files (no data)
- Named `.wh.<original_name>`
- Invisible in directory listings
- Persist in upper_dir indefinitely (until explicitly removed)
- One per deleted file

## Copy-on-Write Semantics

### When Triggered
- File opened with O_WRONLY, O_RDWR, or O_APPEND
- File exists only in lower_dir (read-only)
- File write operation initiated

### Process
1. Copy entire file from lower_dir to upper_dir
2. Preserve original file permissions
3. Proceed with write operation on upper_dir copy

### Result
- Multiple containers can read same lower file
- First writer gets isolated copy
- Lower file remains pristine
- Efficient space usage (copy-on-write pattern)

### Example Scenario
```
Time 0:
  lower: /file.txt (100 MB)
  upper: (empty)
  containers: 5 running, all read /file.txt

Time 1: Container 2 modifies /file.txt
  → copy /file.txt from lower to upper (100 MB copied)
  → write changes to upper copy
  
  lower: /file.txt (100 MB, pristine)
  upper: /file.txt (modified)
  
Time 2: All 5 containers read from their view
  Container 2: sees modified version (upper)
  Containers 1,3,4,5: see original (lower)
```

## Thread Safety

**Current Implementation:** Not thread-safe internally

**FUSE Guarantees:**
- Single-threaded by default (serialized requests)
- Each request gets its own call stack
- Safe within single callback

**Per-Callback Safety:**
- resolve_path() is stateless
- Helper functions use stack-local buffers
- No global state modifications except through explicit file operations

**Potential Issue:**
- If multiple threads add different files simultaneously, no conflict (filesystem handles it)
- If same file modified by multiple processes, last writer wins (standard filesystem behavior)

## Performance Tips

### Optimize Lookups
- Whiteout checks are fast (single lstat call)
- Multiple stat calls per operation could be cached in future versions

### Large Files
- CoW copies entire file (unavoidable for correctness)
- Consider pre-warming upper_dir before writes

### Many Files
- Directory merging scans both layers
- Could optimize with inode caching

## Debugging

### Enable Verbose Output
Modify source to add debug statements:
```c
#define DEBUG 1

#if DEBUG
fprintf(stderr, "DEBUG: resolve_path(%s) -> layer=%d\n", path, *layer);
#endif
```

### Trace System Calls
```bash
strace -e open,unlink,read,write ./mini_unionfs lower upper mnt
```

### Mount with FUSE Debug
```bash
./mini_unionfs -d lower upper mnt  # -d for debug output
```

## Common Implementation Patterns

### Implementing a new FUSE operation

```c
static int unionfs_newop(const char *path, ...) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[MAX_PATH_LEN];
    int layer;
    
    // 1. Resolve the path
    int ret = resolve_path(path, resolved_path, &layer);
    if (ret != 0) {
        return ret;  // File not found or whiteouted
    }
    
    // 2. Check which layer the file is in
    if (layer == 2) {
        // File is in lower_dir (read-only)
        // Decision: CoW or error?
    } else if (layer == 1) {
        // File is in upper_dir (read-write)
        // Safe to modify directly
    }
    
    // 3. Perform operation on resolved path
    // 4. Return result
    return system_call_result ? 0 : -errno;
}
```

### Register new operation
Add to `unionfs_oper` struct:
```c
static const struct fuse_operations unionfs_oper = {
    // ... existing operations ...
    .newop = unionfs_newop,
};
```

## References

- [FUSE API](https://libfuse.github.io/)
- [fuse.h source](https://github.com/libfuse/libfuse/blob/master/include/fuse.h)
- [Linux man pages](https://man7.org/): open, read, write, stat, etc.
