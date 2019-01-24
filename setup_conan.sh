#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"

conan remote add conan-community https://api.bintray.com/conan/conan-community/conan
conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan
conan remote add huang https://api.bintray.com/conan/huangminghuang/conan

conan install ${SCRIPT_DIR}