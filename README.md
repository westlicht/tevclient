# tevclient &nbsp;&nbsp; ![](https://github.com/westlicht/tevclient/workflows/CI/badge.svg)

This is a C++ library to remotly control the [tev image viewer](https://github.com/Tom94/tev). The code for this library is based on the original implementation in tev with some notable modifications:

- C-style interface for passing data instead of using STL containers
- Zero copy when sending image data
- Use error return codes instead of exceptions

## Usage

See [example.cpp](example/example.cpp) for an example on how to use this library.
