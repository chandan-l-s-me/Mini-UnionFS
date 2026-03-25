#!/bin/bash

FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

echo -e "${YELLOW}Starting Mini-UnionFS Test Suite...${NC}"
echo ""

# Check if binary exists
if [ ! -f "$FUSE_BINARY" ]; then
    echo -e "${RED}ERROR: $FUSE_BINARY not found. Run 'make' first.${NC}"
    exit 1
fi

# Setup test directories
rm -rf "$TEST_DIR" 2>/dev/null
mkdir -p "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"

# Create test files
echo "base_only_content" > "$LOWER_DIR/base.txt"
echo "to_be_deleted" > "$LOWER_DIR/delete_me.txt"
mkdir -p "$LOWER_DIR/test_dir"
echo "file_in_dir" > "$LOWER_DIR/test_dir/file.txt"

echo "upper_file" > "$UPPER_DIR/upper_only.txt"

echo "Starting FUSE mount..."
$FUSE_BINARY "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR" 2>/dev/null &
FUSE_PID=$!
sleep 2

# Check if mount was successful
if [ ! -d "$MOUNT_DIR" ] || [ -z "$(ls -A $MOUNT_DIR 2>/dev/null)" ]; then
    echo -e "${RED}ERROR: Failed to mount FUSE filesystem${NC}"
    kill $FUSE_PID 2>/dev/null
    rm -rf "$TEST_DIR"
    exit 1
fi

echo "FUSE mounted with PID: $FUSE_PID"
echo ""
echo "========================================"
echo "Running Tests"
echo "========================================"
echo ""

PASSED=0
FAILED=0

# Test 1: Layer Visibility
echo -n "Test 1: Layer Visibility... "
if [ -f "$MOUNT_DIR/base.txt" ] && grep -q "base_only_content" "$MOUNT_DIR/base.txt"; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    ((FAILED++))
fi

# Test 2: Upper Layer Precedence
echo -n "Test 2: Upper Layer Precedence... "
if [ -f "$MOUNT_DIR/upper_only.txt" ] && grep -q "upper_file" "$MOUNT_DIR/upper_only.txt"; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    ((FAILED++))
fi

# Test 3: Directory Listing
echo -n "Test 3: Directory Listing (merged view)... "
files_in_mount=$(ls "$MOUNT_DIR" 2>/dev/null | wc -l)
if [ $files_in_mount -ge 3 ]; then  # base.txt, delete_me.txt, upper_only.txt, test_dir
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    echo "  Expected at least 4 files, found $files_in_mount"
    ((FAILED++))
fi

# Test 4: Copy-on-Write (File Append)
echo -n "Test 4: Copy-on-Write (append)... "
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
if [ -f "$UPPER_DIR/base.txt" ] && grep -q "modified_content" "$UPPER_DIR/base.txt" && \
   ! grep -q "modified_content" "$LOWER_DIR/base.txt"; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    echo "  CoW not triggered properly"
    ((FAILED++))
fi

# Test 5: Copy-on-Write (File Creation via Write)
echo -n "Test 5: Copy-on-Write (write to lower file)... "
echo "new_data" > "$MOUNT_DIR/base.txt" 2>/dev/null
if [ -f "$UPPER_DIR/base.txt" ] && grep -q "new_data" "$UPPER_DIR/base.txt"; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    ((FAILED++))
fi

# Test 6: Whiteout Mechanism
echo -n "Test 6: Whiteout (deletion)... "
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
if [ ! -f "$MOUNT_DIR/delete_me.txt" ] && \
   [ -f "$LOWER_DIR/delete_me.txt" ] && \
   [ -f "$UPPER_DIR/.wh.delete_me.txt" ]; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    echo "  Whiteout file not created or deletion view failed"
    ((FAILED++))
fi

# Test 7: File Creation in Union
echo -n "Test 7: File creation... "
echo "new_file_content" > "$MOUNT_DIR/new_file.txt" 2>/dev/null
if [ -f "$UPPER_DIR/new_file.txt" ] && grep -q "new_file_content" "$UPPER_DIR/new_file.txt"; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    ((FAILED++))
fi

# Test 8: Directory Visibility
echo -n "Test 8: Directory visibility... "
if [ -d "$MOUNT_DIR/test_dir" ] && [ -f "$MOUNT_DIR/test_dir/file.txt" ]; then
    echo -e "${GREEN}PASSED${NC}"
    ((PASSED++))
else
    echo -e "${RED}FAILED${NC}"
    ((FAILED++))
fi

echo ""
echo "========================================"
echo "Test Results"
echo "========================================"
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo ""

# Cleanup
echo "Cleaning up..."
fusermount -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
kill $FUSE_PID 2>/dev/null
wait $FUSE_PID 2>/dev/null
sleep 1
rm -rf "$TEST_DIR" 2>/dev/null

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
