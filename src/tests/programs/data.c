#include <stdio.h>
#include "../tests/kvm_api.h"

int main(int argc, char **argv)
{
	printf("Data program says %s!\n", argv[1]);
}

EMBED_BINARY(my_data, "inn.png");

__attribute__((used))
void my_backend(const char *arg)
{
	const char ctype[] = "image/png";
	backend_response(ctype, sizeof(ctype)-1, my_data, my_data_size);
}
