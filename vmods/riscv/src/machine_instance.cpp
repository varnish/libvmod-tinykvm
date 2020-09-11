#include "machine_instance.hpp"
// functions available to all machines created during init
std::vector<const char*> riscv_lookup_wishlist {
	"on_init",
	"on_client_request",
	"on_hash",
	"on_synth",
	"on_backend_fetch",
	"on_backend_response"
};
