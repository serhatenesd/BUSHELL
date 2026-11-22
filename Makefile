CC := gcc
CFLAGS := -Wall -Wextra -pedantic
# Keep the implementation and its public headers together as build dependencies.
SOURCES := src/main.c src/shell.c src/commands.c src/filesystem.c
HEADERS := src/shell.h src/commands.h src/filesystem.h

.PHONY: default grade clean

# Build the shell executable from all source modules.
default: bushell

bushell: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

# Build first, then run the test harness.
grade: bushell
	python3 test/grader.py ./bushell test-cases $(if $(findstring s,$(MAKEFLAGS)),--quiet,)

# Remove generated executable and test output.
clean:
	rm -f bushell
	rm -rf my_outputs
