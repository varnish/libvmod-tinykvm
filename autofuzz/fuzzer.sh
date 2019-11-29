#!/bin/bash
#
# 1. stop old fuzzer
# 2. git fetch changes
# 3. checkout latest branch
# 4. ninja build
# 5. start new fuzzer
#
VCP="ON"
BUILD_FOLDER="./build"
CMAKE_FOLDER="$HOME/github/varnish_autoperf/cmake"
GIT_BRANCH="varnish/6.0-plus"
GIT_LOCAL_BRANCH="fuzzy"

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
	LD_LIBRARY_PATH=$HOME/llvm/install/lib $BUILD_FOLDER/varnishd
	return $?
}

function update_repository
{
	pushd ../ext/varnish-cache-plus
	git branch -d $GIT_LOCAL_BRANCH
	git checkout $GIT_BRANCH -b $GIT_LOCAL_BRANCH
	git pull --rebase=true
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
