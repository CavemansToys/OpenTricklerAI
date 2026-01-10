#!/usr/bin/env python3
"""
Create combined bootloader+app UF2 for initial Pico flashing

UF2 Block Format (512 bytes):
  Offset 0-3:   Magic start (0x0A324655)
  Offset 4-7:   Magic end (0x9E5D5157)
  Offset 8-11:  Flags
  Offset 12-15: Target address
  Offset 16-19: Payload size (256 bytes)
  Offset 20-23: Block number (sequential, 0-indexed)
  Offset 24-27: Total number of blocks
  Offset 28-31: File size or family ID
  Offset 32-287: Data payload (256 bytes)
  Offset 508-511: Final magic (0x0AB16F30)
"""
import sys
import struct

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512
UF2_DATA_SIZE = 476  # Full payload area (not just 256 used bytes)

def parse_uf2_block(block_data):
    """Parse a UF2 block and return its components"""
    if len(block_data) != UF2_BLOCK_SIZE:
        raise ValueError(f"Invalid block size: {len(block_data)}")

    # Unpack header
    magic_start0, magic_start1, flags, target_addr, payload_size, block_no, num_blocks, file_size = \
        struct.unpack('<8I', block_data[0:32])

    # Verify magic numbers
    if magic_start0 != UF2_MAGIC_START0 or magic_start1 != UF2_MAGIC_START1:
        raise ValueError(f"Invalid UF2 magic: 0x{magic_start0:08X} 0x{magic_start1:08X}")

    # Extract FULL data payload area (32-507 = 476 bytes)
    # This preserves both used data and any padding/alignment bytes
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
    """Create a UF2 block with updated block numbers"""
    block = bytearray(UF2_BLOCK_SIZE)

    # Pack header
    struct.pack_into('<8I', block, 0,
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        block_info['flags'],
        block_info['target_addr'],
        block_info['payload_size'],
        new_block_no,
        new_total_blocks,
        block_info['file_size']
    )

    # Copy FULL data payload area (476 bytes from offset 32-507)
    block[32:508] = block_info['data']

    # Pack final magic at offset 508
    struct.pack_into('<I', block, 508, UF2_MAGIC_END)

    return bytes(block)

def read_uf2(filename):
    """Read UF2 file and return list of parsed blocks"""
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
    """Write UF2 blocks to file with corrected block numbers"""
    total_blocks = len(blocks)

    with open(filename, 'wb') as f:
        for i, block_info in enumerate(blocks):
            block_data = create_uf2_block(block_info, i, total_blocks)
            f.write(block_data)

def main():
    if len(sys.argv) != 4:
        print("Usage: create_combined_image.py bootloader.uf2 app.uf2 combined.uf2")
        sys.exit(1)

    bootloader_file = sys.argv[1]
    app_file = sys.argv[2]
    output_file = sys.argv[3]

    print(f"Reading {bootloader_file}...")
    bootloader_blocks = read_uf2(bootloader_file)
    print(f"  - Parsed {len(bootloader_blocks)} blocks")
    print(f"  - Address range: 0x{bootloader_blocks[0]['target_addr']:08X} - 0x{bootloader_blocks[-1]['target_addr']:08X}")

    print(f"\nReading {app_file}...")
    app_blocks = read_uf2(app_file)
    print(f"  - Parsed {len(app_blocks)} blocks")
    print(f"  - Address range: 0x{app_blocks[0]['target_addr']:08X} - 0x{app_blocks[-1]['target_addr']:08X}")

    print(f"\nCombining blocks...")
    combined_blocks = bootloader_blocks + app_blocks
    total_blocks = len(combined_blocks)
    print(f"  - Total blocks: {total_blocks}")
    print(f"  - Total size: {total_blocks * UF2_BLOCK_SIZE} bytes (UF2 file)")

    print(f"\nWriting {output_file} with corrected block numbers...")
    write_uf2(output_file, combined_blocks)

    print(f"\n✓ Combined image created successfully!")
    print(f"  Bootloader: blocks 0-{len(bootloader_blocks)-1}")
    print(f"  App:        blocks {len(bootloader_blocks)}-{total_blocks-1}")
    print(f"  Total:      {total_blocks} blocks")

if __name__ == '__main__':
    main()
