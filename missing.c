#include <string.h>
#undef explicit_bzero
void explicit_bzero (void* ptr, const size_t len)
{
  memset(ptr, 0, len);
  __asm__ volatile ("" : : : "memory");
}
