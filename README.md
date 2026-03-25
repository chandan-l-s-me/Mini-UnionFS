# Mini-UnionFS: A FUSE-based Union Filesystem

A userspace implementation of Union Filesystem (UnionFS) using FUSE, demonstrating the layered filesystem mechanism that powers Docker containers.

## Overview

Mini-UnionFS stacks two directories:
- **Lower Directory** (read-only): Base image layer
- **Upper Directory** (read-write): Container layer

The result is a unified, virtual filesystem with:
- **Copy-on-Write (CoW)**: Writing to lower files creates copies in upper
- **Whiteout Files**: Deletions are marked with `.wh.` prefix files
- **Merged Views**: Listing directories shows combined contents

## Quick Start

### Prerequisites
- Linux (Ubuntu 22.04 LTS or compatible)
- FUSE 3.x: `sudo apt-get install libfuse-dev`
- Build tools: `sudo apt-get install build-essential`

### Build
```bash
make
```

### Mount
```bash
mkdir -p /tmp/mnt_test
./mini_unionfs /path/to/lower /path/to/upper /tmp/mnt_test
```

### Test
```bash
make test
```

### Unmount
```bash
fusermount -u /tmp/mnt_test  # On Linux
# or
umount /tmp/mnt_test          # On macOS
```

## Features

✅ **Layer Stacking**: Merge read-only base with read-write container layer  
✅ **Copy-on-Write**: Lazy file copying on first write  
✅ **Whiteout Mechanism**: Mark deletions without affecting lower layer  
✅ **Directory Merging**: Unified view of both layer contents  
✅ **POSIX Operations**: Read, write, create, delete, list directories  

## Project Structure

```
.
├── src/
│   └── mini_unionfs.c       # Main FUSE implementation
├── include/
│   └── mini_unionfs.h       # Header definitions
├── tests/
│   └── test_unionfs.sh      # Automated test suite
├── docs/
│   └── DESIGN.md            # Comprehensive design document
├── Makefile                 # Build configuration
├── README.md                # This file
└── .gitignore              # Git ignore patterns
```

## Design Highlights

### Path Resolution
The core logic that determines which layer a file comes from:
```
Is file whiteouted? → ENOENT
Does it exist in upper? → Use upper version
Does it exist in lower? → Use lower version
Neither? → ENOENT
```

### Copy-on-Write
When opening a file for writing:
```
if (file is in lower_dir AND writing requested)
    copy_file(lower_dir/file, upper_dir/file)
    proceed with write to upper_dir/file
```

### Deletion with Whiteout
When deleting a file:
```
if (file in upper_dir only)
    unlink(upper/file)
else if (file in lower_dir)
    create_empty_file(upper/.wh.filename)
    # Lower file is now hidden from all operations
```

## Testing

Run the automated test suite:
```bash
make test
```

Tests validate:
- Layer visibility and precedence
- Copy-on-Write functionality
- Whiteout deletion mechanism
- File creation and directory operations
- Merged directory listings

## Implementation Details

See [docs/DESIGN.md](docs/DESIGN.md) for:
- Complete architecture overview
- Data structure specifications
- Edge case handling
- Performance considerations
- Error handling strategy

## Supported POSIX Operations

| Operation | Supported |
|-----------|-----------|
| open (read/write) | ✓ |
| read | ✓ |
| write | ✓ |
| create | ✓ |
| unlink (delete) | ✓ |
| mkdir | ✓ |
| rmdir | ✓ |
| readdir (list) | ✓ |
| getattr (stat) | ✓ |
| rename | ✓ |
| truncate | ✓ |

## Known Limitations

- No symbolic link support
- No special files (sockets, devices) support
- No file locking mechanisms
- No extended attributes
- Directory listings not cached

## Building on Different Systems

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install libfuse-dev build-essential
make
```

### Fedora/RHEL
```bash
sudo dnf install fuse-devel gcc
make
```

### macOS (if FUSE for macOS installed)
```bash
brew install macfuse
make
```

## Troubleshooting

**Mount fails with "No such device"**: 
- FUSE kernel module not loaded
- On Linux: Check `/proc/filesystems` for `fuse` entry

**Permission denied on files**:
- Check permissions on lower_dir and upper_dir
- Both should be readable by the mounting user

**"fusermount: command not found"**:
- Install FUSE: `sudo apt-get install fuse`

## Educational Value

This project demonstrates:
- FUSE kernel-userspace communication
- Filesystem layering concepts
- Copy-on-Write optimization technique
- Whiteout file semantics
- Union mounting strategies

Perfect for understanding how Docker manages container filesystems!

## Authors

Cloud Computing course project - SEM6, CC_MP

## References

- [FUSE Documentation](https://github.com/libfuse/libfuse)
- [Union Filesystem Overview](https://en.wikipedia.org/wiki/Union_mount)
- [Docker Overlay2 Driver](https://docs.docker.com/storage/storagedriver/overlayfs-driver/)

## License

This is a course project for educational purposes.
