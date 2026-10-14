#!/usr/bin/env python3
import sys
import os

c_slash_slash = chr(47) + chr(47)
c_slash_star = chr(47) + chr(42)
c_star_slash = chr(42) + chr(47)

def check_file(filepath):
    errors = []
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()

    for idx, line in enumerate(lines, 1):
        line_str = line

        for char in line_str:
            if ord(char) > 127:
                errors.append(f"Line {idx}: Non-ASCII / potential emoji character '{char}' (U+{ord(char):04X})")

        if c_slash_slash in line_str:
            errors.append(f"Line {idx}: Contains double slash comment marker: {line_str.strip()}")

        if c_slash_star in line_str:
            errors.append(f"Line {idx}: Contains slash star comment start marker: {line_str.strip()}")
        if c_star_slash in line_str:
            errors.append(f"Line {idx}: Contains star slash comment end marker: {line_str.strip()}")

    return errors

def main():
    target_files = sys.argv[1:]
    if not target_files:
        print("Usage: check_clean_code.py <file1.c> <file2.h> ...")
        sys.exit(1)

    total_errors = 0
    for file_path in target_files:
        if not os.path.exists(file_path):
            print(f"File not found: {file_path}")
            continue
        errors = check_file(file_path)
        if errors:
            print(f"FAILED: {file_path}")
            for err in errors:
                print(f"  {err}")
            total_errors += len(errors)
        else:
            print(f"PASSED: {file_path} is clean.")

    if total_errors > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
