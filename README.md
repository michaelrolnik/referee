# Installation

## Linux
Install the following tools
```bash
sudo apt-get install clang-format
sudo apt-get install cmake
sudo apt-get install g++
sudo apt-get install gcc
sudo apt-get install libcli11-dev
sudo apt-get install libfmt-dev
sudo apt-get install libfmt8
sudo apt-get install libgtest-dev
sudo apt-get install libgtextutils-dev
sudo apt-get install llvm
```

## MacOS
```
brew install antlr 
brew install antlr4-cpp-runtime
brew install clang-format
brew install cli11
brew install cmake
brew install fmt
brew install googletest
brew install llvm
brew install spdlog
```


# Checkout
```bash
git clone git@github.com:michaelrolnik/referee.git
cd referee
git submodule init
git submodule update
```

# Building
## Regular
```bash
cd referee
cmake -GNinja ..
ninja
```
### Run Tests
```bash
./tests
```

## Code Coverage
```bash
cd referee
cmake -GNinja -DCODE_COVERAGE=ON ..
ninja
```

### Run Tests
(This does not work on MacOS)
```bash
ninja ccov-tests
```
