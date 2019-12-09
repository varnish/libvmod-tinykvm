#!/bin/bash
#
# 1. stop old fuzzer
# 2. git fetch changes
# 3. checkout latest branch
# 4. ninja build
# 5. start new fuzzer
#
# $1 = Fuzzing mode (HTTP, HTTP2, etc.)
# $2 = Build folder
#
VCP="ON"
REPO_FOLDER="$PWD/.."
BUILD_FOLDER="$REPO_FOLDER/autofuzz/${2:-build}"
CMAKE_FOLDER="$REPO_FOLDER/cmake"
GIT_BRANCH="6.0-plus"
export CC=$HOME/llvm/install/bin/clang-10
export CXX=$HOME/llvm/install/bin/clang++-10

set -x
set -e
export ASAN_OPTIONS=disable_coredump=0::unmap_shadow_on_exit=1
export ASAN_SYMBOLIZER_PATH=$HOME/llvm/install/bin/llvm-symbolizer
# set coredump location to a known place
mkdir -p /tmp/crash
echo '/tmp/crash/core_%e.%p' | sudo tee /proc/sys/kernel/core_pattern

function stop_fuzzer
{
	# can't do this, it will kill other services running fuzzers
	#if pgrep "varnishd"; then pkill "varnishd"; fi
	return $?
}

function start_fuzzer
{
	LOGID=$(head /dev/urandom | tr -dc a-z0-9 | head -c8)
	# HTTP parser fuzzing
	LD_LIBRARY_PATH=$HOME/llvm/install/lib $BUILD_FOLDER/varnishd -fork=2 > fuzz-$LOGID.log
	return $?
}

function update_repository
{
	pushd $REPO_FOLDER/ext/varnish-cache-plus
	git fetch origin
	git reset --hard origin/$GIT_BRANCH
	popd
}

function build_fuzzer
{
	local OPTIONS="-DSINGLE_PROCESS=ON -DVARNISH_PLUS=$VCP -DLIBFUZZER=ON -DSANITIZE=ON"
	local FUZZTYPE="-DFUZZER=$1"
	mkdir -p $BUILD_FOLDER
	pushd $BUILD_FOLDER
	# Note: Remove -G Ninja to use regular make
	cmake $CMAKE_FOLDER -G Ninja $OPTIONS $FUZZTYPE
	ninja
	popd
}

echo ">>> Stopping fuzzer..."
stop_fuzzer
echo ">>> Retrieving repository changes..."
update_repository
echo ">>> Building new fuzzer of type $1..."
build_fuzzer $1
echo ">>> Starting fuzzer..."
start_fuzzer
echo "Exit code: $?"
