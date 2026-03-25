# Quick Start Guide - Mini-UnionFS

Get up and running with Mini-UnionFS in 5 minutes!

## TL;DR

```bash
# On Ubuntu/Debian
sudo apt-get install libfuse-dev build-essential

# Build
cd CC_MP
make

# Test
make test

# Manual test
mkdir -p /tmp/lower /tmp/upper /tmp/mnt
echo "hello" > /tmp/lower/file.txt
./mini_unionfs /tmp/lower /tmp/upper /tmp/mnt
cat /tmp/mnt/file.txt           # Should print: hello
echo "world" > /tmp/mnt/file.txt  # Triggers Copy-on-Write
cat /tmp/upper/file.txt         # Should print: world
cat /tmp/lower/file.txt         # Should print: hello (unchanged)
fusermount -u /tmp/mnt
```

## Prerequisites

### For Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install -y build-essential libfuse-dev
```

### For Fedora:
```bash
sudo dnf install -y gcc make pkg-config fuse-devel
```

### For Arch:
```bash
sudo pacman -S base-devel fuse3
```

## Step 1: Build

```bash
cd /path/to/CC_MP
make
```

Expected output:
```
mkdir -p build
gcc -Wall -Wextra -O2 -g -Iinclude -c src/mini_unionfs.c -o build/mini_unionfs.o
gcc -Wall -Wextra -O2 -g -Iinclude -o mini_unionfs build/mini_unionfs.o -lfuse
Build successful: mini_unionfs
```

## Step 2: Prepare Test Directories

```bash
# Create test directories
mkdir -p /tmp/unionfs_test/lower
mkdir -p /tmp/unionfs_test/upper
mkdir -p /tmp/unionfs_test/mnt

# Add test files to lower (read-only layer)
echo "This is a base file" > /tmp/unionfs_test/lower/base.txt
mkdir -p /tmp/unionfs_test/lower/subdir
echo "Nested file" > /tmp/unionfs_test/lower/subdir/nested.txt
```

## Step 3: Mount the Filesystem

```bash
./mini_unionfs /tmp/unionfs_test/lower /tmp/unionfs_test/upper /tmp/unionfs_test/mnt
```

You should see:
```
Mini-UnionFS mounted:
  Lower (read-only): /tmp/unionfs_test/lower
  Upper (read-write): /tmp/unionfs_test/upper
  Mount point: /tmp/unionfs_test/mnt
```

## Step 4: Test Operations

### List merged directory
```bash
ls -la /tmp/unionfs_test/mnt
# Shows: base.txt, subdir
```

### Read from lower layer
```bash
cat /tmp/unionfs_test/mnt/base.txt
# Output: This is a base file
```

### Trigger Copy-on-Write (modify lower file)
```bash
echo "Modified!" > /tmp/unionfs_test/mnt/base.txt
```

Check where the modification went:
```bash
cat /tmp/unionfs_test/upper/base.txt  # Should have "Modified!"
cat /tmp/unionfs_test/lower/base.txt  # Should still have "This is a base file"
```

### Create new file
```bash
echo "New file" > /tmp/unionfs_test/mnt/new.txt
ls -la /tmp/unionfs_test/upper/
# new.txt should appear here
```

### Delete file from lower layer (Whiteout)
```bash
rm /tmp/unionfs_test/mnt/base.txt
ls -la /tmp/unionfs_test/mnt/
# base.txt is gone!
ls -la /tmp/unionfs_test/upper/
# .wh.base.txt whiteout marker should exist
cat /tmp/unionfs_test/lower/base.txt  # Still there!
```

## Step 5: Unmount

```bash
# Linux
fusermount -u /tmp/unionfs_test/mnt

# Or alternative
sudo umount /tmp/unionfs_test/mnt

# Verify unmounted
ls /tmp/unionfs_test/mnt  # Should be empty again
```

## Automated Test Suite

Run comprehensive tests:

```bash
make test
```

This runs `tests/test_unionfs.sh` which validates:
- ✓ Layer visibility
- ✓ Upper layer precedence
- ✓ Directory merging
- ✓ Copy-on-Write
- ✓ Whiteout deletion
- ✓ File creation
- ✓ Nested directories

Expected output:
```
Starting Mini-UnionFS Test Suite...
Starting FUSE mount...
FUSE mounted with PID: 12345

========================================
Running Tests
========================================

Test 1: Layer Visibility... PASSED
Test 2: Upper Layer Precedence... PASSED
Test 3: Directory Listing (merged view)... PASSED
Test 4: Copy-on-Write (append)... PASSED
Test 5: Copy-on-Write (write)... PASSED
Test 6: Whiteout (deletion)... PASSED
Test 7: File creation... PASSED
Test 8: Directory visibility... PASSED

========================================
Test Results
========================================
Passed: 8
Failed: 0

All tests passed!
```

## What's Happening Under the Hood

### Layer Resolution
When you access `/tmp/unionfs_test/mnt/file.txt`, the system:
1. Checks if `.wh.file.txt` exists in upper → If yes, file is deleted
2. Checks if `file.txt` exists in upper → Use upper version
3. Checks if `file.txt` exists in lower → Use lower version
4. Not found → Error

### Copy-on-Write Example
```
Initial state:
  lower/base.txt = "original"
  upper/base.txt = (doesn't exist)

You execute: echo "modified" >> /mnt/base.txt

Internally:
  1. mini_unionfs detects write to lower layer file
  2. Copies /tmp/unionfs_test/lower/base.txt → /tmp/unionfs_test/upper/base.txt
  3. Appends "modified" to /tmp/unionfs_test/upper/base.txt

Final state:
  lower/base.txt = "original" (unchanged)
  upper/base.txt = "original\nmodified"
  /mnt/base.txt shows upper version = "original\nmodified"
```

### Whiteout Example
```
Initial state:
  lower/delete_me.txt = "content"
  upper/ = (empty)

You execute: rm /mnt/delete_me.txt

Internally:
  1. mini_unionfs detects deletion of lower layer file
  2. Creates empty marker file: /tmp/unionfs_test/upper/.wh.delete_me.txt
  3. During resolve_path(), if .wh. exists, return ENOENT

Final state:
  lower/delete_me.txt = "content" (pristine)
  upper/.wh.delete_me.txt = (empty marker)
  /mnt/delete_me.txt = NOT VISIBLE (hidden by whiteout)
```

## Common Operations

### Create a directory in mount
```bash
mkdir /tmp/unionfs_test/mnt/mydir
ls -la /tmp/unionfs_test/upper/  # mydir appears here
```

### Copy file into mount
```bash
cp /etc/hostname /tmp/unionfs_test/mnt/
cat /tmp/unionfs_test/upper/hostname  # Should show hostname
```

### Modify existing file (CoW)
```bash
cat /tmp/unionfs_test/mnt/base.txt | head
sed -i 's/base/modified/g' /tmp/unionfs_test/mnt/base.txt
cat /tmp/unionfs_test/upper/base.txt | head  # Modified version
cat /tmp/unionfs_test/lower/base.txt | head  # Original pristine
```

### Compare layers
```bash
# See what's unique in each layer
diff -r /tmp/unionfs_test/lower /tmp/unionfs_test/upper

# See only modifications (not in lower)
ls -la /tmp/unionfs_test/upper/ | grep -v "^d"

# See only deleted files (whiteouts)
ls -la /tmp/unionfs_test/upper/ | grep "^[^d].*\.wh\."
```

## Troubleshooting

### Mount fails: "No such device"
**Cause**: FUSE not available
```bash
# Check FUSE module
grep fuse /proc/filesystems

# Load it
sudo modprobe fuse
```

### Mount fails: "Device or connection refused"
**Cause**: Another process using mount point
```bash
# Kill processes using mount point
sudo fuser -k /tmp/unionfs_test/mnt

# Or clean mount point
fusermount -u /tmp/unionfs_test/mnt 2>/dev/null
```

### "Permission denied" when creating files
**Cause**: Parent directory permissions
```bash
# Ensure upper dir is writable
chmod 755 /tmp/unionfs_test/upper
```

### Mount succeeds but can't see files
**Cause**: FUSE daemon crashed silently
```bash
# Try verbose mount
./mini_unionfs -d /tmp/unionfs_test/lower /tmp/unionfs_test/upper /tmp/unionfs_test/mnt
# Should show error messages

# Check if process running
ps aux | grep mini_unionfs
```

## Next Steps

1. **Explore the Design**: Read [docs/DESIGN.md](../docs/DESIGN.md)
2. **Understand the API**: Read [docs/API.md](../docs/API.md)
3. **Review the Code**: Read [src/mini_unionfs.c](../src/mini_unionfs.c)
4. **Advanced Testing**: Create your own test cases
5. **Extend Features**: Add new POSIX operations or optimizations

## Architecture Diagram

```
┌─────────────────────────────────────────┐
│   User Application                      │
│   (ls, cat, echo, touch, etc.)          │
└──────────────────┬──────────────────────┘
                   │ File system calls
                   ▼
┌─────────────────────────────────────────┐
│   Linux VFS (Virtual File System)       │
│   (Red Hat kernel)                      │
└──────────────────┬──────────────────────┘
                   │ FUSE protocol
                   ▼
┌─────────────────────────────────────────┐
│   Mini-UnionFS Daemon                   │
│   (mini_unionfs binary)                 │
│                                         │
│   ┌─────────────────────────────────┐   │
│   │ resolve_path()                  │   │
│   │ - Check whiteout                │   │
│   │ - Check upper precedence        │   │
│   │ - Fallback to lower             │   │
│   └─────────────────────────────────┘   │
└──────────────┬──────────────┬───────────┘
               │              │
        ┌──────▼──┐    ┌──────▼──────┐
        │  Upper  │    │  Lower      │
        │  Dir    │    │  Dir        │
        │(read/w) │    │(read-only)  │
        └─────────┘    └─────────────┘
```

## File Structure After Operations

```
Initial:
  lower/
    ├── base.txt
    ├── subdir/
    │   └── nested.txt
  upper/
  mnt/ → (union mount point)

After modifications:
  lower/
    ├── base.txt              (unchanged)
    ├── subdir/
    │   └── nested.txt        (unchanged)
  upper/
    ├── base.txt              (CoW copy, modified)
    ├── new.txt               (created)
    ├── .wh.deleted_file.txt  (whiteout marker)
  mnt/ → shows merged view
    ├── base.txt              (from upper, modified)
    ├── new.txt               (from upper, new)
    ├── subdir/               (from lower, unchanged)
    │   └── nested.txt        (from lower, unchanged)
    (deleted_file.txt not visible - hidden by whiteout)
```

## Performance Expectations

- First file read: ~1ms
- First file write (CoW): ~100ms (depends on file size)
- Subsequent reads/writes: ~1-5ms
- Directory listing: proportional to total entries

## Learn More

- **Design Document**: [docs/DESIGN.md](../docs/DESIGN.md)
- **API Reference**: [docs/API.md](../docs/API.md)
- **Build Instructions**: [docs/BUILD.md](../docs/BUILD.md)
- **Source Code**: [src/mini_unionfs.c](../src/mini_unionfs.c)
- **Full README**: [README.md](../README.md)

---

**Ready to explore Mini-UnionFS?** Start with the test sequence above, then dive into the design documentation!
