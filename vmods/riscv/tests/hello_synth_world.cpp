#include "mini_api.hpp"

extern "C" __attribute__((visibility("hidden"), used))
void start(int argc, char** argv)
{
	exit(0);
}

static const char reason[] = "synth";

void on_recv()
{
	decision(reason, sizeof(reason)-1, 404);
}

static const char mime[] = "text/plain";
static const char text[] = "Hello World";

void on_synth()
{
	synth(mime, sizeof(mime)-1, text, sizeof(text)-1);
}
