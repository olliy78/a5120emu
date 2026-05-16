#!/usr/bin/env python3
"""
K1520 Emulator - Detailed Boot Diagnostics
============================================

Provides detailed information about the boot process:
- CPU state
- Memory inspection
- Disk activity
- Display updates
- Interrupt status
"""

import sys
import time
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from app.core_binding.k1520 import K1520Emulator


def test_detailed_boot():
    """Test booting with detailed diagnostics."""
    print("=" * 70)
    print("K1520 A5120 Detailed Boot Diagnostics")
    print("=" * 70)
    print()
    
    # Locate disk image
    disk_path = Path(__file__).parent / "disk_b.img"
    if not disk_path.exists():
        print(f"ERROR: {disk_path} not found")
        return False
    
    # Create and initialize emulator
    print("1. INITIALIZATION")
    print("-" * 70)
    
    print("Creating emulator...", end=" ")
    emu = K1520Emulator()
    print("✓")
    
    print("Powering on...", end=" ")
    emu.power_on()
    print("✓")
    
    print("Mounting disk_b.img on drive 0...", end=" ")
    if not emu.mount_disk(0, str(disk_path), "cpa800", False):
        print("✗ FAILED")
        print("  (Cannot mount disk - stopping test)")
        return False
    print("✓")
    
    print("Disk active:", emu.is_disk_active(0))
    print("Disk write-protected:", emu.is_disk_write_protected(0))
    print()
    
    # Run boot sequence in stages
    print("2. BOOT SEQUENCE")
    print("-" * 70)
    
    stages = [
        (1000,   "Bootstrap ROM execution"),
        (5000,   "BIOS initialization"),
        (10000,  "Disk boot attempt"),
        (15000,  "CP/M kernel load"),
        (10000,  "System stabilization"),
    ]
    
    total_cycles = 0
    fb_updates = 0
    
    for cycles, desc in stages:
        print(f"\nStage: {desc}")
        print(f"  Running {cycles:,} cycles...", end=" ")
        
        cycles_run = 0
        initial_fb_dirty = emu.is_framebuffer_dirty()
        
        for _ in range(0, cycles, 1000):
            cycles_run += emu.run(1000)
            if emu.is_framebuffer_dirty():
                fb_updates += 1
                emu.clear_framebuffer_dirty_flag()
        
        total_cycles += cycles_run
        print(f"✓ ({cycles_run:,} cycles)")
        
        # Display state
        fb = emu.get_framebuffer()
        nonzero = sum(1 for b in fb if b not in (0, 0xFF))
        print(f"  Framebuffer: {len(fb)} bytes, {nonzero} non-trivial bytes")
        print(f"  FB updates: {fb_updates}")
    
    print()
    print("3. RESULTS")
    print("-" * 70)
    
    fb = emu.get_framebuffer()
    zero_bytes = sum(1 for b in fb if b == 0)
    ff_bytes = sum(1 for b in fb if b == 0xFF)
    other_bytes = len(fb) - zero_bytes - ff_bytes
    
    print(f"Total cycles executed: {total_cycles:,}")
    print(f"Framebuffer updates: {fb_updates}")
    print(f"Final framebuffer state:")
    print(f"  Total bytes:     {len(fb):,}")
    print(f"  Zero bytes:      {zero_bytes:6,} ({100*zero_bytes//len(fb):3d}%)")
    print(f"   0xFF bytes:      {ff_bytes:6,} ({100*ff_bytes//len(fb):3d}%)")
    print(f"  Other bytes:     {other_bytes:6,} ({100*other_bytes//len(fb):3d}%)")
    
    print()
    
    # Diagnosis
    print("4. DIAGNOSIS")
    print("-" * 70)
    
    if other_bytes > 0 or fb_updates > 0:
        print("✓ GOOD: Display shows activity (boot banner/prompt expected)")
        
        # Try to decode some display content
        print("\n  Framebuffer dump (first 40 bytes as hex):")
        for i in range(0, min(40, len(fb)), 8):
            hex_str = " ".join(f"{b:02x}" for b in fb[i:i+8])
            print(f"    {i:04d}: {hex_str}")
        
        return True
    else:
        print("⚠ UNCERTAIN: Framebuffer is empty or all same value")
        print("  Possible causes:")
        print("  - ROM may not have been loaded (CPU reset issue)")
        print("  - Disk boot may have failed")
        print("  - Display controller not responding")
        print("  - Operating system didn't load")
        return False


if __name__ == "__main__":
    try:
        success = test_detailed_boot()
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
        success = False
    
    print()
    print("=" * 70)
    sys.exit(0 if success else 1)
