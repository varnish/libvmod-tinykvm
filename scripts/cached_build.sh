FILE=/tmp/timings.txt

build_object() {
    echo "Building object: $1" >> $FILE
    cp $1 /tmp/code.c
    /usr/bin/time -p -a -o $FILE \
    ccache gcc -c -O2 -Wall -Werror -Wno-error=unused-result -pthread -fpic -fexceptions -o /tmp/temp.o $1
}
build_so() {
    echo "Building final shared object: $2" >> $FILE
    /usr/bin/time -p -a -o $FILE \
    ccache gcc -O2 -pthread -fpic -fexceptions -shared -Wl,-x -o $2 /tmp/temp.o
}

build_object $1 $2
build_so $1 $2
