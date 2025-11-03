#!/usr/bin/env python3
"""
Fix compile_commands.json by removing GCC-specific flags that clang-tidy doesn't understand.
"""

import json
import sys
from pathlib import Path

def fix_compile_commands(input_file: Path, output_file: Path = None):
    """Remove GCC-specific flags from compile_commands.json"""

    if output_file is None:
        output_file = input_file

    # Flags to remove (GCC-specific, not understood by clang)
    flags_to_remove = [
        '-mno-direct-extern-access',
    ]

    print(f"Reading {input_file}...")
    with open(input_file, 'r') as f:
        commands = json.load(f)

    print(f"Processing {len(commands)} compilation units...")
    modified_count = 0

    for entry in commands:
        original_command = entry['command']
        modified_command = original_command

        # Remove each problematic flag
        for flag in flags_to_remove:
            if flag in modified_command:
                modified_command = modified_command.replace(flag, '')
                modified_command = ' '.join(modified_command.split())  # Clean up double spaces

        if modified_command != original_command:
            entry['command'] = modified_command
            modified_count += 1

    print(f"Modified {modified_count} compilation units")

    # Backup original if we're overwriting
    if output_file == input_file:
        backup_file = input_file.with_suffix('.json.backup')
        print(f"Creating backup: {backup_file}")
        with open(backup_file, 'w') as f:
            json.dump(commands, f, indent=2)

    print(f"Writing fixed compile_commands.json to {output_file}...")
    with open(output_file, 'w') as f:
        json.dump(commands, f, indent=2)

    print("Done!")

if __name__ == '__main__':
    compile_commands_path = Path('build/compile_commands.json')

    if not compile_commands_path.exists():
        print(f"Error: {compile_commands_path} not found!", file=sys.stderr)
        sys.exit(1)

    fix_compile_commands(compile_commands_path)
