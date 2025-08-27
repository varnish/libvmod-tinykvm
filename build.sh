#!/bin/bash
set -e

#!/bin/bash
set -e
#export CC="ccache $CC"
#export CXX="ccache $CXX"
cmake_extra=""
build_type="Release"
varnish_plus="OFF"

usage() {
	echo "Usage: $0 [-v] [--enterprise]"
	echo "  -v            verbose build"
	echo "  --enterprise  build for Varnish Enterprise"
	exit 1
}

for i in "$@"; do
	case $i in
		--enterprise)
            varnish_plus="ON"
            shift
            ;;
		--sanitize)
			build_type="Debug"
			cmake_extra="-DSANITIZE=ON"
			shift
			;;
		--no-sanitize)
			build_type="Release"
			cmake_extra="-DSANITIZE=OFF"
			shift
			;;
		--debug)
			build_type="Debug"
			cmake_extra="-DSANITIZE=OFF"
			shift
			;;
		--release)
			build_type="Release"
			cmake_extra="-DSANITIZE=OFF"
			shift
			;;
		-v)
			export VERBOSE=1
			shift
			;;
		-*|--*)
			echo "Unknown option $i"
			exit 1
			;;
		*)
		;;
	esac
done

mkdir -p .build
pushd .build
cmake .. -DCMAKE_BUILD_TYPE=$build_type -DVARNISH_PLUS=$varnish_plus $cmake_extra
cmake --build . -j6
popd

VPATH="/usr/lib/varnish/vmods/"
VEPATH="/usr/lib/varnish-plus/vmods/"
if [ "$varnish_plus" == "ON" ]; then
	VPATH=$VEPATH
fi

echo "Installing vmod into $VPATH"
sudo cp .build/libvmod_*.so $VPATH
