#!/usr/bin/env python3
"""
Merge bootloader.uf2 + app.uf2 with correct RP2350 family ID

CRITICAL: Fix family ID for RP2350 blocks
- Bootloader UF2 includes boot2 (0x10000000) + bootloader (0x10000100)
- App UF2 is at 0x10006000
- Must fix family ID to 0xe48bff57 for RP2350
"""
import sys
import struct

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512
UF2_FLAG_FAMILY_ID = 0x00002000
RP2XXX_ABSOLUTE_FAMILY_ID = 0xe48bff57  # For both RP2040 and RP2350

def parse_uf2_block(block_data):
    """Parse UF2 block and return components"""
    if len(block_data) != UF2_BLOCK_SIZE:
        raise ValueError(f"Invalid block size: {len(block_data)}")

    # Parse header
    magic_start0, magic_start1, flags, target_addr, payload_size, block_no, num_blocks, file_size = \
        struct.unpack('<8I', block_data[0:32])

    # Verify magic
    if magic_start0 != UF2_MAGIC_START0 or magic_start1 != UF2_MAGIC_START1:
        raise ValueError(f"Invalid UF2 magic: 0x{magic_start0:08X} 0x{magic_start1:08X}")

    # Extract full payload area (476 bytes)
    data_payload = block_data[32:508]

    # Verify final magic
    final_magic = struct.unpack('<I', block_data[508:512])[0]
    if final_magic != UF2_MAGIC_END:
        raise ValueError(f"Invalid final magic: 0x{final_magic:08X}")

    return {
        'flags': flags,
        'target_addr': target_addr,
        'payload_size': payload_size,
        'block_no': block_no,
        'num_blocks': num_blocks,
        'file_size': file_size,
        'data': data_payload
    }

def create_uf2_block(block_info, new_block_no, new_total_blocks):
    """Create UF2 block with updated block numbers and RP2350 family ID"""
    block = bytearray(UF2_BLOCK_SIZE)

    # Force family ID flag and use RP2XXX_ABSOLUTE for RP2350
    flags = block_info['flags'] | UF2_FLAG_FAMILY_ID

    # Pack header with RP2350 family ID
    struct.pack_into('<8I', block, 0,
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        flags,
        block_info['target_addr'],
        block_info['payload_size'],
        new_block_no,
        new_total_blocks,
        RP2XXX_ABSOLUTE_FAMILY_ID  # RP2350 family ID
    )

    # Copy full payload area
    block[32:508] = block_info['data']

    # Pack final magic
    struct.pack_into('<I', block, 508, UF2_MAGIC_END)

    return bytes(block)

def read_uf2(filename):
    """Read UF2 file and parse blocks"""
    blocks = []
    with open(filename, 'rb') as f:
        while True:
            data = f.read(UF2_BLOCK_SIZE)
            if len(data) < UF2_BLOCK_SIZE:
                break
            try:
                block_info = parse_uf2_block(data)
                blocks.append(block_info)
            except ValueError as e:
                print(f"Warning: Skipping invalid block: {e}")
                continue
    return blocks

def write_uf2(filename, blocks):
    """Write UF2 with corrected block numbers and family ID"""
    total_blocks = len(blocks)

    with open(filename, 'wb') as f:
        for i, block_info in enumerate(blocks):
            block_data = create_uf2_block(block_info, i, total_blocks)
            f.write(block_data)

def main():
    if len(sys.argv) != 4:
        print("Usage: create_combined_uf2.py bootloader.uf2 app.uf2 combined.uf2")
        sys.exit(1)

    bootloader_file = sys.argv[1]
    app_file = sys.argv[2]
    output_file = sys.argv[3]

    print(f"Reading {bootloader_file}...")
    bootloader_blocks = read_uf2(bootloader_file)
    print(f"  - Parsed {len(bootloader_blocks)} blocks")
    if bootloader_blocks:
        print(f"  - Address range: 0x{bootloader_blocks[0]['target_addr']:08X} - 0x{bootloader_blocks[-1]['target_addr']:08X}")

    print(f"\nReading {app_file}...")
    app_blocks = read_uf2(app_file)
    print(f"  - Parsed {len(app_blocks)} blocks")
    if app_blocks:
        print(f"  - Address range: 0x{app_blocks[0]['target_addr']:08X} - 0x{app_blocks[-1]['target_addr']:08X}")

    print(f"\nCombining blocks...")
    combined_blocks = bootloader_blocks + app_blocks
    total_blocks = len(combined_blocks)
    print(f"  - Total blocks: {total_blocks}")
    print(f"  - Total size: {total_blocks * UF2_BLOCK_SIZE} bytes")

    print(f"\nFixing family ID to 0x{RP2XXX_ABSOLUTE_FAMILY_ID:08X} (RP2XXX_ABSOLUTE for RP2350)...")
    print(f"Writing {output_file} with corrected block numbers...")
    write_uf2(output_file, combined_blocks)

    print(f"\n✓ Combined UF2 created successfully!")
    print(f"  Bootloader+boot2: blocks 0-{len(bootloader_blocks)-1}")
    print(f"  App:              blocks {len(bootloader_blocks)}-{total_blocks-1}")
    print(f"  Total:            {total_blocks} blocks")
    print(f"  Family ID:        0x{RP2XXX_ABSOLUTE_FAMILY_ID:08X} (RP2350)")

if __name__ == '__main__':
    main()
