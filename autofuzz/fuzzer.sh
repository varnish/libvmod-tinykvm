#!/bin/bash
#
# 1. stop old fuzzer
# 2. git fetch changes
# 3. checkout latest branch
# 4. ninja build
# 5. start new fuzzer
#
VCP="ON"
REPO_FOLDER="$PWD/.."
BUILD_FOLDER="$REPO_FOLDER/autofuzz/build"
CMAKE_FOLDER="$REPO_FOLDER/cmake"
GIT_BRANCH="6.0-plus"

set -x
set -e

function stop_fuzzer
{
	# might kill someone else?
	if pgrep "varnishd"; then pkill "varnishd"; fi
	return $?
}

function start_fuzzer
{
	ulimit -c unlimited
	# HTTP parser fuzzing
	LD_LIBRARY_PATH=$HOME/llvm/install/lib $BUILD_FOLDER/varnishd -fork=6 -use_value_profile=1 -only_ascii=1 > fuzz-0.log
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
	mkdir -p $BUILD_FOLDER
	pushd $BUILD_FOLDER
	# Note: Remove -G Ninja to use regular make
	cmake $CMAKE_FOLDER -G Ninja $OPTIONS
	ninja
	popd
}

echo ">>> Stopping fuzzer..."
stop_fuzzer
echo ">>> Retrieving repository changes..."
update_repository
echo ">>> Building new fuzzer..."
build_fuzzer
echo ">>> Starting fuzzer..."
start_fuzzer
echo "Exit code: $?"
