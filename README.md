# Bushell

Bushell is a Unix-like shell implemented in C with an in-memory filesystem.
Directories, files, and links exist only for the lifetime of the program; the
shell does not read or modify the host computer's filesystem.

## Requirements

- GCC
- GNU Make
- Python 3 (for the test grader)

## Build and run

Build the executable:

```sh
make
```

Start the shell:

```sh
./bushell
```

Enter one command per line. Type `exit` to leave the shell. The filesystem
starts empty each time the program is launched.

Commands can also be read from a file:

```sh
./bushell < test-cases/input1.txt
```

## Supported commands

| Command | Behavior |
| --- | --- |
| `pwd` | Print the current working directory. |
| `cd <directory>` | Change to an existing directory using an absolute or relative path. Symbolic links are followed up to 16 levels, and the resolved path must be a directory. |
| `ls [directory]` | List a directory; hidden names are omitted by default. |
| `ls -a [directory]` | Include hidden names and display `./` and `../`. |
| `tree` | Print the current directory and its real subdirectories recursively; symbolic links are not traversed. |
| `mkdir <path>` | Create one directory. Its parent directory must already exist. |
| `touch <file>` | Create an empty file if the path does not resolve to an existing file. |
| `cat <file>` | Print a file's contents. |
| `du <file>` | Print the file's size in bytes and the supplied path. |
| `rm <path>` | Remove a file, link, or empty directory, but not the root or current directory. A symbolic link is removed without removing its target. |
| `ln <source-file> <link>` | Create a hard link to a regular file, sharing its inode and contents. Directory and symbolic-link sources are rejected. |
| `ln -s <target-file> <link>` | Create a symbolic link to an existing regular file. Directory and symbolic-link targets are rejected. Relative targets are interpreted from the link's directory. |
| `cp <source-file> <destination-file>` | Copy file contents to the destination, creating it if needed. The destination's parent directory must exist. |
| `mv <source> <destination>` | Move or rename a file, directory, or link. The destination parent must exist and the destination name must not already exist. |
| `echo <text...>` | Print the text to standard output. |
| `echo <text...> > <file>` | Replace the destination file's contents with the text and a newline. |
| `echo <text...> >> <file>` | Append the text and a newline to the destination file. |
| `exit` | Exit the shell. |

Command arguments can be grouped with double quotes. A backslash escapes the
next character. Invalid commands and failed operations print
`bushell: error`.

## Test

Run the supplied input/output tests with:

```sh
make grade
```

The grader runs each `test-cases/input*.txt` file, removes interactive prompts
from the shell output, and compares the result with the matching expected
output file. Normalized outputs are saved in `my_outputs/`.

## Project files

```text
src/main.c                    Shell input loop and process state
src/shell.c, src/shell.h      Command-line tokenization
src/commands.c, src/commands.h  Command validation and dispatch
src/filesystem.c, src/filesystem.h  In-memory filesystem and operations
test/grader.py                Test runner and output comparison
test-cases/                   Test inputs and expected outputs
my_outputs/                   Generated test output
Makefile                      Build, test, and clean targets
```

## Clean

Remove the generated shell executable and test output:

```sh
make clean
```
