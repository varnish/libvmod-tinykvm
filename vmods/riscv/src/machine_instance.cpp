#include "machine_instance.hpp"

void MachineInstance::add_reference() const
{
    std::atomic_fetch_add_explicit (&refcount, 1u, std::memory_order_relaxed);
}

void MachineInstance::remove_reference() const
{
    if ( std::atomic_fetch_sub_explicit (&refcount, 1u, std::memory_order_release) == 1 ) {
         std::atomic_thread_fence(std::memory_order_acquire);
         delete this;
    }
}
