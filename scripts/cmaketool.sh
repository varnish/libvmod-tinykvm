#!/bin/bash
CLANG="clang"
CLANGPP="clang++"
if command -v "clang-11" &> /dev/null; then
	CLANG="clang-11"
	CLANGPP="clang++-11"
fi
if command -v "clang-12" &> /dev/null; then
	CLANG="clang-12"
	CLANGPP="clang++-12"
fi
if command -v "clang-13" &> /dev/null; then
	CLANG="clang-13"
	CLANGPP="clang++-13"
fi
if command -v "clang-14" &> /dev/null; then
	CLANG="clang-14"
	CLANGPP="clang++-14"
fi
# WARNING: Produces too new debug info for GDB
#if command -v "clang-15" &> /dev/null; then
#	CLANG="clang-15"
#	CLANGPP="clang++-15"
#fi
echo "Detected Clang: ${CLANG}, ${CLANGPP}"
export CC="ccache ${CLANG}"
export CXX="ccache ${CLANGPP}"
BUILD_PATH="$PWD"
SOURCE_DIR="$PWD"

# TODO: scrutinize me
preargs=
args="-DVCL_DIR=$PWD -DSYSTEM_OPENSSL=ON -DPython3_EXECUTABLE=/usr/bin/python3"
vcp="OFF"
do_build=false
do_clean=false
do_gprof=false
do_sanitize=false
do_debug=false
do_singleproc=false
folder="build_vc"
run=false
set -e

setup_build() {
	mkdir -p $BUILD_PATH/$folder
	pushd $BUILD_PATH/$folder
	cmake $SOURCE_DIR -G Ninja -DVARNISH_PLUS=$vcp -DVMOD_DIR=$PWD -DUSE_LLD=ON $args
	popd
}

edit_build() {
	pushd $BUILD_PATH/$folder
	ccmake $SOURCE_DIR
	popd
}

ninja_clean() {
	pushd $BUILD_PATH/$folder
	ninja clean
	popd
}

ninja_build() {
	pushd $BUILD_PATH/$folder
	ninja
	popd
}

for i in "$@"
do
case $i in
    --vcp=*)
    vcp="ON"
	folder="${i#*=}"
    shift # past argument and value
    ;;
	--vc=*)
    vcp="OFF"
	folder="${i#*=}"
    shift # past argument and value
    ;;
	--edit)
    shift # past argument with no value
	edit_build
    ;;
    --fuzz=*)
	fuzzer="${i#*=}"
	echo "Fuzzer enabled: $fuzzer"
    args="$args -DLIBFUZZER=ON -DFUZZER=$fuzzer"
    shift # past argument and value
	;;
	--disable-fuzzer)
	args="$args -DLIBFUZZER=OFF -UFUZZER"
    shift
    ;;
    --sanitize)
	do_sanitize=true
    shift
    ;;
	--gprof)
	do_gprof=true
    shift
    ;;
	--debug)
    args="$args -DCMAKE_BUILD_TYPE=Debug -DLTO_ENABLE=OFF -DNATIVE=OFF"
	do_debug=true
    shift
    ;;
	--optimize)
    args="$args -DCMAKE_BUILD_TYPE=Release -DLTO_ENABLE=ON -DNATIVE=ON"
    shift
    ;;
	--no-optimize)
    args="$args -DCMAKE_BUILD_TYPE= -DLTO_ENABLE=OFF -DNATIVE=OFF"
    shift
    ;;
	--mold)
    args="$args -DUSE_MOLD=ON"
    shift
    ;;
	--disable-mold)
    args="$args -DUSE_MOLD=OFF"
    shift
    ;;
	--static-kvm)
	args="$args -DSHARED_KVM=OFF"
    shift
    ;;
	--shared-kvm)
	args="$args -DSHARED_KVM=ON"
    shift
    ;;
	--static-riscv)
	args="$args -DSHARED_LIBRISCV=OFF"
    shift
    ;;
	--shared-riscv)
	args="$args -DSHARED_LIBRISCV=ON"
    shift
    ;;
	--mgt-process)
	do_singleproc=false
    shift
    ;;
	--single-process)
	do_singleproc=true
    shift
    ;;
	--disable-numa)
	preargs="numactl -N 0 $preargs"
    shift
    ;;
	--build)
	do_build=true
    shift
    ;;
	--clean)
	do_clean=true
    shift
    ;;
	--run)
	run=true
	shift
    break
    ;;
    *)
      # unknown option
	  echo "$0"
	  echo "--vc=[build folder]"
	  echo "--vcp=[build folder]"
	  echo "--edit		Show CMake TUI"
	  echo "--build		Build with ninja"
	  echo "--clean		Clean build folder (ninja clean)"
	  echo "--sanitize	Sanitize with asan"
	  echo "--optimize	Enable all optimizations"
	  echo "--static-kvm		Link with KVM sandbox directly"
	  echo "--shared-kvm		Build KVM sandbox as a shared library"
	  echo "--static-riscv		Link with RISC-V sandbox directly"
	  echo "--shared-riscv		Build RISC-V sandbox as a shared library"
	  echo "--mgt-process		Run child in subprocess (default)"
	  echo "--single-process	Run child in same process as mgt"
	  echo "--disable-numa		Force varnishd to run on NUMA node 0"
	  echo "--run	[args...]	Start varnishd with arguments"
	  echo "--fuzz	[subsystem]"
	  echo "	Subsystems: HTTP, HTTP2, RANDOM, RESPONSE_H1, RESPONSE_H2"
	  echo "	Subsystems: RESPONSE_H1, RESPONSE_H2, RESPONSE_GZIP"
	  echo "	Subsystems: PROXY, PROXY2, HPACK, VMOD, ..."
	  echo "--disable-fuzzer"
	  echo "--help"
	  exit 1
    ;;
esac
done

if [ "$do_debug" = true ] ; then
	args="$args -DDEBUGGING=ON"
else
	args="$args -DDEBUGGING=OFF"
fi
if [ "$do_gprof" = true ] ; then
	args="$args -DGPROF=ON"
else
	args="$args -DGPROF=OFF"
fi
if [ "$do_sanitize" = true ] ; then
	args="$args -DSANITIZE=ON"
else
	args="$args -DSANITIZE=OFF"
fi
if [ "$do_singleproc" = true ] ; then
	args="$args -DSINGLE_PROCESS=ON"
else
	args="$args -DSINGLE_PROCESS=OFF"
fi

setup_build
if [ "$do_clean" = true ] ; then
	ninja_clean
fi
if [ "$do_build" = true ] ; then
	ninja_build
fi
if [ "$run" = true ] ; then
	pushd $BUILD_PATH/$folder

	if [ "$do_debug" = true ] ; then
		gdb --args ./varnishd "$@"
	else
		[ "$VERBOSE" ] && echo "$preargs ./varnishd $@"
		$preargs ./varnishd "$@"
	fi
	popd
fi
