# Building EOS with Conan Package Manager

Conan is typically described as a "decentralized package manager for C and C++”. It enables users to encapsulate C/C++ project dependencies, distribute them, and consume them in other projects. This involves the complex challenges of transitive dependencies, versioning, licensing, and so forth.

It has a client-server architecture which allows clients to fetch packages from, as well as upload packages to, different servers (“remotes”), similar to the “git” push-pull model to/from git remotes. Each package contains the *recipe* to describe the location of  source code, how to build the binaries and the metadata required to consume the package. A package can be optionally bundled with the binary artifacts based on the different combinations of OS, machine architecture, compiler, compiler version and build type. 

The Conan server is a TCP server that can be easily run as your own server on-premises to host your private packages. JFrog Bintray provides a public and free hosting service for OSS Conan packages. Users can create their own repositories under their accounts and organizations, and freely upload Conan packages there, without moderation. 

For now, all dependent packages used by EOS are hosted on Bintray. Some of them are from the [conqn-center](https://bintray.com/conan/conan-center), [conan-community](https://bintray.com/conan-community/conan) or [bincrafters](https://bintray.com/bincrafters/public-conan); others are created by Huang-Ming Huang and hosted on his personal [repository](https://bintray.com/huangminghuang/conan). Here are the list of conan packages being used EOS:

- Zlib 1.2.11
- Boost 1.67.0
- OpenSSL 1.0.2q
- GMP 6.1.2
- [Mongo-c-driver 1.13.0](https://github.com/huangminghuang/conan-mongo-c-driver.git)
- [Mongo-cxx-driver 3.4.0](https://github.com/huangminghuang/conan-mongo-cxx-driver.git)
- [LLVM 4.0.1](https://github.com/huangminghuang/conan-llvm.git)

Beside the above pacakges, EOS also depends on GNU *gettext*. However, there is no working conan gettext package available and  I believe it would be less an effort to converting EOS to use *Boost Locale* (which I think Johnathon Giszczak already did) than to develop a *gettext* recipe. 

## Install Conan

### All Platforms with pip installed

    $ pip install conan

### On CentOS/RHEL with DevToolSet 7 installed
 
    $ sudo yum install python34-pip
    $ pip3 install --user conan

### Ubuntu 18,04
   
    $ sudo apt update && sudo apt install python-pip
    $ pip install conan
    $ export PATH=$HOME/.local/bin:$PATH

### MacOS with Homebrew installed

    $ brew update
    $ brew install conan

## Building EOS

At the moment, building EOS with conan is only available on the *conan* branch from the Huang-Ming Huang's fork of EOS. The basic requreiments are:

- git
- make or ninja
- Compiler: GCC 7+ or Clang 4+
- cmake 3.5+
- conan
- gettext 

Fetching conan packages from remotes is part of the cmake configure step and transparent to uses. 

    $ git clone https://github.com/huangminghuang/eos.git
    $ cd eos && git checkout conan
    $ git submodule update --init --recursive
    $ cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release
    $ cmake --build build

Notice that *CMAKE_BUILD_TYPE* must be specified because the cmake script uses it to decide which kind of dependent binaries (release or debug) to download. 

If there's no pre-built dependent binaries available for your build environment, the cmake script would use the connan recipe to compile and install the dependencies on your machine.

