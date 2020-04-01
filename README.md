# B+ tree (static)

This is an implementation of the **static** B+ tree.
This implementation has the following features and limitations:
- **STATIC: one has to provide all data in advance to construct the tree; insertion and deletion are not designed;**
- it's written in C++ and is compilable into a standalone shared library (see [usage example](./b-plus-tree/test/test-shared-lib.cpp))
- the storage is abstracted via the interface
- storage component can be either in-memory, or file system (binary file), one can extend it to use database or external storage
- the storage is modeled as RAM (from the user perspective)
- if using file system adapter, one can shutdown the tree and restart it using the file
- the solution is tested, the coverage is 100%
- the solution is benchmarked
- the solution is documented, the documentation is [online](https://bplustree.dbogatov.org/)
- user inputs are screened (exceptions are thrown if the input is invalid)
- Makefile is sophisticated - with simple commands one can compile and run tests and benchmarks
- the code is formatted according to [clang file](./.clang-format)
- there are VS Code configs that let one debug the code with breakpoints

## How to compile and run

Dependencies:
- for building a shared library
	- `g++` that supports `--std=c++17`
	- `make`
	- this lib `-l boost_system` (boost)
- for testing and benchmarking
	- all of the above
	- these libs `-l gtest -l pthread -l benchmark` (Google Test and Google Benchmark with pthread)
	- `gcovr` for coverage
	- `doxygen` for generating docs
- or, use this Docker image: `dbogatov/docker-images:pbc-latest`

[Makefile](./b-plus-tree/Makefile) is used for everything.

```bash
# to compile the shared library (libbplustree.so in ./bin) and run example code against it
make clean run-shared

# to compile and run all unit tests
make clean run-tests

# to compile and run all integration tests (usually heavier than units)
make clean run-integration

# to compile and run all benchmarks
make clean run-benchmarks

# to compute unit test coverage
make clean coverage

# to generate documentation
make clean docs
```
