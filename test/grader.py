#!/usr/bin/env python3
import os
import sys
import subprocess
import glob
import re

def clean_shell_output(raw_output):
    """
    Removes interactive prompts while preserving one output line for each
    submitted command. Prompts can appear in the middle of a line because
    commands such as mkdir do not print a newline.
    """
    prompt_pattern = re.compile(r'user@boun:[^\n$]*\$[ \t]*')
    prompts = list(prompt_pattern.finditer(raw_output))

    if not prompts:
        return [line.rstrip() for line in raw_output.splitlines()]

    cleaned_lines = []
    for index, prompt in enumerate(prompts[:-1]):
        end = prompts[index + 1].start()
        command_output = raw_output[prompt.end():end]
        lines = command_output.splitlines()
        if lines:
            cleaned_lines.extend(line.rstrip() for line in lines)
        else:
            cleaned_lines.append('')

    return cleaned_lines

def grade():
    if len(sys.argv) < 3:
        print("Usage: python3 test/grader.py <path_to_executable> <test_cases_dir>")
        sys.exit(1)

    executable = sys.argv[1]
    test_dir = sys.argv[2]
    quiet = "--quiet" in sys.argv[3:]
    output_dir = "my_outputs"

    os.makedirs(output_dir, exist_ok=True)

    input_files = sorted(
        glob.glob(os.path.join(test_dir, "input*.txt")),
        key=lambda x: int(re.search(r'\d+', os.path.basename(x)).group()) if re.search(r'\d+', os.path.basename(x)) else x
    )

    if not input_files:
        print(f"No input files found in {test_dir}")
        sys.exit(1)

    passed_tests = 0

    for input_path in input_files:
        filename = os.path.basename(input_path)
        test_num = filename.replace("input", "").replace(".txt", "")
        
        expected_output_path = os.path.join(test_dir, f"output{test_num}.txt")
        actual_output_path = os.path.join(output_dir, f"my_output{test_num}.txt")

        with open(input_path, "r") as f:
            input_data = f.read()

        try:
            result = subprocess.run(
                [executable],
                input=input_data,
                capture_output=True,
                text=True,
                timeout=5
            )
            actual_stdout = result.stdout
        except Exception as e:
            if not quiet:
                print(f"Test {test_num}: Execution failed ({e})")
            continue

        actual_lines = clean_shell_output(actual_stdout)
        while actual_lines and actual_lines[-1] == '':
            actual_lines.pop()

        with open(actual_output_path, "w") as f:
            f.write("\n".join(actual_lines) + ("\n" if actual_lines else ""))

        if not os.path.exists(expected_output_path):
            if not quiet:
                print(f"Test {test_num}: Missing expected output file {expected_output_path}")
            continue

        with open(expected_output_path, "r") as f:
            expected_stdout = f.read()

        expected_lines = [line.rstrip() for line in expected_stdout.splitlines()]
        while expected_lines and expected_lines[-1] == '':
            expected_lines.pop()

        total_lines = max(len(expected_lines), len(actual_lines))
        matching_lines = 0

        for idx in range(total_lines):
            exp_line = expected_lines[idx] if idx < len(expected_lines) else None
            act_line = actual_lines[idx] if idx < len(actual_lines) else None
            
            if exp_line == act_line:
                matching_lines += 1

        status = "PASS" if matching_lines == total_lines and len(expected_lines) == len(actual_lines) else "FAIL"
        if status == "PASS":
            passed_tests += 1
        if not quiet:
            print(f"Test {test_num}: {matching_lines}/{total_lines} lines matched -> {status}")

    if quiet:
        print(f"{passed_tests}/{len(input_files)}")

if __name__ == "__main__":
    grade()