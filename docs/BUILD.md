# Mini-UnionFS Build Instructions

## System Requirements

### Linux (Recommended)
- **OS**: Ubuntu 22.04 LTS (or Debian/Fedora/Arch equivalents)
- **Kernel**: 5.x or higher (FUSE support)
- **FUSE**: Version 3.x
- **Compiler**: GCC 9+ or Clang 10+

### macOS
Mini-UnionFS requires FUSE support, which is not native on modern macOS. Options:
1. **Use a Linux VM** (VirtualBox, Parallels, UTM)
2. **Use WSL2** (Windows Subsystem for Linux, if on Windows)
3. Install [macFUSE](https://osxfuse.github.io/) and use compatibility mode

### Windows
Use Windows Subsystem for Linux (WSL2) with Ubuntu:
```bash
wsl --install Ubuntu-22.04
```

Then follow Linux instructions inside WSL.

## Installation Steps

### Step 1: Install Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    pkg-config \
    libfuse-dev \
    git
```

#### Fedora/RHEL
```bash
sudo dnf install -y \
    gcc \
    make \
    pkg-config \
    fuse-devel \
    git
```

#### Arch Linux
```bash
sudo pacman -S \
    base-devel \
    fuse3 \
    git
```

### Step 2: Clone/Navigate to Repository
```bash
cd /path/to/CC_MP
```

### Step 3: Build
```bash
# Using the build script (recommended)
bash build.sh

# Or directly
make
```

### Step 4: Verify Build
```bash
ls -la mini_unionfs
file ./mini_unionfs
```

Expected output:
```
./mini_unionfs: ELF 64-bit LSB executable, x86-64, ...
```

## Building on Different Systems

### Ubuntu Desktop (Local Machine)
```bash
sudo apt-get install libfuse-dev build-essential
make
./mini_unionfs --version  # May not work without FUSE in kernel
```

### Ubuntu Server
```bash
sudo apt-get install libfuse-dev build-essential
make
# Ready to mount and test
```

### Docker Container (Best for Testing)
```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    libfuse-dev \
    git

WORKDIR /app
COPY . .
RUN make

ENTRYPOINT ["./mini_unionfs"]
```

Build and run:
```bash
docker build -t mini-unionfs .
docker run --privileged -it mini-unionfs /lower /upper /mnt
```

### Virtual Machine (VirtualBox/KVM/UTM)
1. Create Ubuntu 22.04 VM with minimum 2GB RAM, 10GB disk
2. Install in VM:
   ```bash
   sudo apt-get install libfuse-dev build-essential
   make
   ```
3. Test on VM filesystem

## Build Output

### On Success
```
$ make
mkdir -p build
gcc -Wall -Wextra -O2 -g -I/usr/include/fuse3 -Iinclude -c src/mini_unionfs.c -o build/mini_unionfs.o
gcc -Wall -Wextra -O2 -g -I/usr/include/fuse3 -Iinclude -o mini_unionfs build/mini_unionfs.o -lfuse
Build successful: mini_unionfs
```

### On Failure - Missing pkg-config
```
make: pkg-config: No such file or directory
→ Install: sudo apt-get install pkg-config
```

### On Failure - Missing FUSE
```
fatal error: 'fuse.h' file not found
→ Install: sudo apt-get install libfuse-dev
```

## Troubleshooting Build Issues

### Issue: "fuse.h: No such file or directory"
**Cause**: FUSE development libraries not installed
```bash
# Solution
sudo apt-get install libfuse-dev
make clean && make
```

### Issue: "File 'fuse.pc' not found"
**Cause**: pkg-config can't find FUSE
```bash
# Solution
export PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig
make clean && make
```

### Issue: "undefined reference to `fuse_main'"
**Cause**: Linking failure, FUSE library not linked
```bash
# Solution
make clean && make
# Verify with: pkg-config --libs fuse
```

### Issue: "permission denied" during build
**Cause**: User doesn't have write permissions
```bash
# Solution
chmod u+w .
chmod u+rw build/
make clean && make
```

## Verifying the Build

After successful build, verify with:

```bash
# Check binary exists and is executable
file ./mini_unionfs
./mini_unionfs --help    # May exit with usage info or error (expected)

# Check dependencies are resolved
ldd ./mini_unionfs | grep fuse
# Should show: libfuse.so.3 => ...

# Verify main function resolves
nm ./mini_unionfs | grep fuse_main
# Should show: fuse_main
```

## Building Without FUSE (Read-Only, Not Recommended)

For educational purposes only - this won't actually work as a filesystem:

```bash
# Create a minimal stub version (NOT functional)
gcc -c src/mini_unionfs.c -Iinclude -o build/mini_unionfs.o \
    -DNOFUSE  # Imaginary flag

# This will fail - don't do this unless you understand implications
```

## Next Steps After Building

1. **Run automated tests**: `make test`
2. **Manual testing**: See README.md for mounting instructions
3. **Review design**: `cat docs/DESIGN.md`
4. **Explore code**: `less src/mini_unionfs.c`

## Parallel Builds

For faster compilation on multi-core systems:

```bash
make -j4   # Use 4 parallel jobs
```

## Clean Builds

Fully clean and rebuild:

```bash
make clean
make
```

Or using the reset script:
```bash
git clean -fdx  # Removes all ignored files
make
```

## Development Build Flags

For debugging, modify `CFLAGS` in Makefile:

```bash
make CFLAGS="-Wall -Wextra -g -O0 -DDEBUG"
```

This enables:
- `-O0`: No optimization (easier debugging)
- `-g`: Debug symbols
- `-DDEBUG`: Custom debug macro (if added to code)

## Installation to System

After building, you can optionally install:

```bash
sudo cp mini_unionfs /usr/local/bin/
sudo chmod 755 /usr/local/bin/mini_unionfs

# Now available system-wide
which mini_unionfs
```

Uninstall:
```bash
sudo rm /usr/local/bin/mini_unionfs
```

## Getting Help

If build fails:

1. Check GCC version: `gcc --version`
2. Check FUSE version: `pkg-config --modversion fuse`
3. Check FUSE location: `pkg-config --cflags --libs fuse`
4. Review Makefile: `cat Makefile`
5. See full build output: `make clean && make 2>&1 | tee build.log`

## Summary

```bash
# Quick start on Ubuntu/Debian
sudo apt-get install libfuse-dev build-essential
cd /path/to/CC_MP
make
make test  # optional - requires FUSE mounted
```

That's all! You should now have a working Mini-UnionFS binary.
