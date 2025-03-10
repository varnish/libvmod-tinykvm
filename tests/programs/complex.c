#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "../tests/kvm_api.h"

int main(int argc, char **argv)
{
	printf("Hello from %s!\n", argv[1]);
}

__attribute__((used))
extern void my_backend(const char *arg)
{
	char *text;
	for (size_t i = 0; i < 100; i++) {
		text = malloc(4000);
	}
	volatile double f = 1.0;
	f = pow(f, 2.0);
	sprintf(text, "Complex result: %s, %f", arg, f);
	return_result("text/complex", text);
}
