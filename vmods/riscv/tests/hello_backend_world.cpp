#include "mini_api.hpp"

void start(int argc, char** argv)
{
	exit(0);
}

static const char mime[] = "text/plain";
static const char text[] = "Hello World";

void on_recv()
{
	forge(CACHED, [] {
		forge_response(mime, sizeof(mime)-1, text, sizeof(text)-1);
	});
	exit(0);
}
