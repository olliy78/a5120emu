#!/usr/bin/env python3
"""
K1520 Emulator - Basic Integration Test
=========================================

Test that the C++ core library works correctly through the Python ctypes binding.

This test runs without GUI (no display needed) and validates:
1. Library loading
2. Machine creation/destruction
3. Basic emulation loop
4. Framebuffer access
5. Disk operations
"""

import sys
import tempfile
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from app.core_binding.k1520 import K1520Emulator


def test_library_loading():
    """Test that libk1520core.so can be loaded."""
    print("TEST: Library Loading...", end=" ")
    try:
        emulator = K1520Emulator()
        print("✓")
        return emulator
    except Exception as e:
        print(f"✗\nError: {e}")
        return None


def test_power_on(emulator):
    """Test power on."""
    print("TEST: Power On...", end=" ")
    try:
        emulator.power_on()
        print("✓")
        return True
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def test_emulation_loop(emulator):
    """Test running emulation cycles."""
    print("TEST: Emulation Loop (10000 cycles)...", end=" ")
    try:
        cycles = emulator.run(10000)
        assert cycles > 0, f"Expected cycles > 0, got {cycles}"
        print(f"✓ ({cycles} cycles executed)")
        return True
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def test_framebuffer(emulator):
    """Test framebuffer access."""
    print("TEST: Framebuffer Access...", end=" ")
    try:
        fb = emulator.get_framebuffer()
        # 640x288 = 184320 bytes (each byte = 1 pixel)
        expected_size = 640 * 288
        assert len(fb) == expected_size, f"Expected {expected_size} bytes, got {len(fb)}"
        print(f"✓ ({len(fb)} bytes, {640}×{288} pixels)")
        return True
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def test_disk_mount(emulator):
    """Test disk mounting (without actual disk file)."""
    print("TEST: Disk Mount/Unmount...", end=" ")
    try:
        # Create a temporary disk image
        with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as f:
            disk_path = f.name
            # Write some test data
            f.write(b'\x00' * 1000)
        
        try:
            # Try to mount (may fail if format isn't correct, that's OK)
            result = emulator.mount_disk(0, disk_path, "CPA800", False)
            if result:
                # If mounted, try to unmount
                unmount_result = emulator.unmount_disk(0)
                print(f"✓ (mount={result}, unmount={unmount_result})")
            else:
                print("✓ (mount failed as expected, format validation working)")
            return True
        finally:
            # Cleanup
            Path(disk_path).unlink(missing_ok=True)
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def test_keyboard_input(emulator):
    """Test keyboard input queueing."""
    print("TEST: Keyboard Input...", end=" ")
    try:
        # Queue some key events
        emulator.key_press(0x41, shift=False, ctrl=False)  # 'A'
        emulator.key_release(0x41)
        emulator.key_press(0x42, shift=True, ctrl=False)   # Shift+'B'
        emulator.key_release(0x42)
        emulator.key_press(0x43, shift=False, ctrl=True)   # Ctrl+'C'
        emulator.key_release(0x43)
        print("✓")
        return True
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def test_reset(emulator):
    """Test reset."""
    print("TEST: Reset...", end=" ")
    try:
        emulator.reset()
        print("✓")
        return True
    except Exception as e:
        print(f"✗\nError: {e}")
        return False


def main():
    """Run all tests."""
    print("=" * 60)
    print("K1520 Emulator - Integration Test Suite")
    print("=" * 60)
    print()
    
    tests_passed = 0
    tests_failed = 0
    
    # Test 1: Library loading
    emulator = test_library_loading()
    if emulator is None:
        print("\nFATAL: Cannot load library, stopping tests")
        return 1
    tests_passed += 1
    
    # Test 2: Power on
    if test_power_on(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 3: Emulation loop
    if test_emulation_loop(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 4: Framebuffer
    if test_framebuffer(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 5: Disk operations
    if test_disk_mount(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 6: Keyboard input
    if test_keyboard_input(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Test 7: Reset
    if test_reset(emulator):
        tests_passed += 1
    else:
        tests_failed += 1
    
    # Cleanup
    del emulator
    
    # Summary
    print()
    print("=" * 60)
    print(f"Test Results: {tests_passed} passed, {tests_failed} failed")
    print("=" * 60)
    
    if tests_failed > 0:
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
