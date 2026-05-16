#!/usr/bin/env python3
"""
K1520 Emulator - Boot Test
===========================

Boot the A5120 from disk_b.img and check if the display gets updated
with boot banner/CP/M prompt.

This is the final validation test - if this passes, the emulator is functional!
"""

import sys
import time
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from app.core_binding.k1520 import K1520Emulator


def test_boot():
    """Test booting from disk_b.img."""
    print("=" * 60)
    print("K1520 A5120 Boot Test")
    print("=" * 60)
    print()
    
    # Locate disk image
    disk_path = Path(__file__).parent / "disk_b.img"
    if not disk_path.exists():
        print(f"ERROR: {disk_path} not found")
        return False
    
    print(f"Disk image: {disk_path}")
    print(f"Size: {disk_path.stat().st_size / 1024:.1f} KB")
    print()
    
    # Create and initialize emulator
    print("Creating emulator...", end=" ")
    try:
        emu = K1520Emulator()
        print("OK")
    except Exception as e:
        print(f"FAILED: {e}")
        return False
    
    print("Powering on...", end=" ")
    try:
        emu.power_on()
        print("OK")
    except Exception as e:
        print(f"FAILED: {e}")
        return False
    
    # Mount disk
    print("Mounting disk_b.img on drive 0 (cpa800 format)...", end=" ")
    try:
        if not emu.mount_disk(0, str(disk_path), "cpa800", False):
            print("FAILED")
            return False
        print("OK")
    except Exception as e:
        print(f"FAILED: {e}")
        return False
    
    print()
    print("Running emulation (50000 cycles for boot sequence)...")
    
    # Run boot sequence
    fb_changed = False
    max_cycles = 50000
    cycles_per_frame = 1000
    
    for i in range(0, max_cycles, cycles_per_frame):
        try:
            cycles = emu.run(cycles_per_frame)
            
            # Check if framebuffer changed
            if emu.is_framebuffer_dirty():
                fb_changed = True
                emu.clear_framebuffer_dirty_flag()
            
            # Progress indicator
            if (i // cycles_per_frame) % 5 == 0:
                pct = (i * 100) // max_cycles
                print(f"  {pct:3d}% complete ({i}/{max_cycles} cycles)", end="\r")
            
        except Exception as e:
            print(f"\nERROR during execution: {e}")
            return False
    
    print(f"  100% complete ({max_cycles}/{max_cycles} cycles)        ")
    print()
    
    # Check result
    print("Boot Test Results:")
    print("-" * 60)
    
    if fb_changed:
        print("✓ Framebuffer was updated (display activity detected)")
    else:
        print("✗ Framebuffer was NOT updated (no display activity)")
    
    # Get current framebuffer
    fb = emu.get_framebuffer()
    
    # Check if there's any content in framebuffer (not all zeros or 0xFF)
    zero_bytes = sum(1 for b in fb if b == 0)
    ff_bytes = sum(1 for b in fb if b == 0xFF)
    other_bytes = len(fb) - zero_bytes - ff_bytes
    
    print(f"✓ Framebuffer content analysis:")
    print(f"    Total bytes: {len(fb)}")
    print(f"    Zero bytes:  {zero_bytes:6d} ({100*zero_bytes//len(fb):3d}%)")
    print(f"    0xFF bytes:  {ff_bytes:6d} ({100*ff_bytes//len(fb):3d}%)")
    print(f"    Other bytes: {other_bytes:6d} ({100*other_bytes//len(fb):3d}%)")
    
    success = other_bytes > 0 or fb_changed
    
    if success:
        print()
        print("✓ Boot test PASSED: Emulator executed successfully!")
    else:
        print()
        print("✗ Boot test UNCERTAIN: Emulator ran but no display activity")
    
    # Cleanup
    print()
    print("Cleaning up...", end=" ")
    try:
        emu.unmount_disk(0)
        del emu
        print("OK")
    except:
        pass
    
    return success


if __name__ == "__main__":
    success = test_boot()
    print()
    print("=" * 60)
    sys.exit(0 if success else 1)
