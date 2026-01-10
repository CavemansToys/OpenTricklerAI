#!/usr/bin/env python3
"""
Create combined bootloader+app BIN file with proper addressing
Then convert to UF2 format for Raspberry Pi Pico

Memory Layout:
- 0x10000000: Boot2 stage (256 bytes, provided by SDK)
- 0x10000100: Bootloader starts (~16KB)
- 0x10006000: Application Bank A starts (896KB)
"""
import sys
import struct

# Flash memory base for RP2040/RP2350
FLASH_BASE = 0x10000000

# Addresses
BOOT2_ADDR = 0x10000000
BOOTLOADER_ADDR = 0x10000100
APP_ADDR = 0x10006000

# UF2 constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2350_FAMILY_ID = 0xe48bff56  # RP2040 and RP2350 use same family ID
UF2_BLOCK_SIZE = 512
UF2_DATA_SIZE = 256  # Standard page size for Pico

def read_bin(filename):
    """Read binary file"""
    with open(filename, 'rb') as f:
        return f.read()

def create_uf2_block(address, data, block_no, total_blocks):
    """Create a single UF2 block"""
    block = bytearray(UF2_BLOCK_SIZE)

    # Header
    struct.pack_into('<8I', block, 0,
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FLAG_FAMILY_ID,
        address,
        UF2_DATA_SIZE,
        block_no,
        total_blocks,
        RP2350_FAMILY_ID
    )

    # Data payload (pad to 256 bytes if needed)
    data_padded = data + b'\x00' * (UF2_DATA_SIZE - len(data))
    block[32:32+UF2_DATA_SIZE] = data_padded

    # Final magic
    struct.pack_into('<I', block, 508, UF2_MAGIC_END)

    return bytes(block)

def bin_to_uf2_blocks(bin_data, start_address):
    """Convert binary data to UF2 blocks"""
    blocks = []
    offset = 0

    while offset < len(bin_data):
        chunk_size = min(UF2_DATA_SIZE, len(bin_data) - offset)
        chunk = bin_data[offset:offset+chunk_size]
        address = start_address + offset

        # Create block (block numbers will be fixed later)
        block_info = {
            'address': address,
            'data': chunk
        }
        blocks.append(block_info)

        offset += chunk_size

    return blocks

def write_uf2(filename, blocks):
    """Write UF2 file with correct block numbering"""
    total_blocks = len(blocks)

    with open(filename, 'wb') as f:
        for i, block_info in enumerate(blocks):
            block_data = create_uf2_block(
                block_info['address'],
                block_info['data'],
                i,
                total_blocks
            )
            f.write(block_data)

def main():
    if len(sys.argv) != 4:
        print("Usage: create_combined_bin.py bootloader.bin app.bin combined.uf2")
        sys.exit(1)

    bootloader_file = sys.argv[1]
    app_file = sys.argv[2]
    output_file = sys.argv[3]

    print(f"Reading {bootloader_file}...")
    bootloader_bin = read_bin(bootloader_file)
    print(f"  Size: {len(bootloader_bin)} bytes")
    print(f"  Address: 0x{BOOTLOADER_ADDR:08X}")

    print(f"\nReading {app_file}...")
    app_bin = read_bin(app_file)
    print(f"  Size: {len(app_bin)} bytes")
    print(f"  Address: 0x{APP_ADDR:08X}")

    print(f"\nConverting bootloader to UF2 blocks...")
    bootloader_blocks = bin_to_uf2_blocks(bootloader_bin, BOOTLOADER_ADDR)
    print(f"  Created {len(bootloader_blocks)} blocks")

    print(f"\nConverting application to UF2 blocks...")
    app_blocks = bin_to_uf2_blocks(app_bin, APP_ADDR)
    print(f"  Created {len(app_blocks)} blocks")

    print(f"\nCombining blocks...")
    combined_blocks = bootloader_blocks + app_blocks
    total_blocks = len(combined_blocks)
    print(f"  Total: {total_blocks} blocks")
    print(f"  Size: {total_blocks * UF2_BLOCK_SIZE} bytes")

    print(f"\nWriting {output_file}...")
    write_uf2(output_file, combined_blocks)

    print(f"\n✓ Combined UF2 created successfully!")
    print(f"  Bootloader: blocks 0-{len(bootloader_blocks)-1} at 0x{BOOTLOADER_ADDR:08X}")
    print(f"  App:        blocks {len(bootloader_blocks)}-{total_blocks-1} at 0x{APP_ADDR:08X}")

if __name__ == '__main__':
    main()
