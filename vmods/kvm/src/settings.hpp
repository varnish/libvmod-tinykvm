#pragma once

namespace kvm
{
    static constexpr int   REQUEST_VM_NICE = 10;
    static constexpr float ERROR_HANDLING_TIMEOUT = 1.0f;

    /* Serialized storage VM access */
    static constexpr int   STORAGE_VM_NICE = 0;
    static constexpr float STORAGE_TIMEOUT = 3.0f;
    static constexpr float STORAGE_CLEANUP_TIMEOUT = 1.0f;
    static constexpr int   STORAGE_TASK_MAX_TIMERS = 15;
    /* Async storage VM access */
    static constexpr float ASYNC_STORAGE_TIMEOUT = 3.0f;
    static constexpr int   ASYNC_STORAGE_NICE = 15;
    static constexpr bool  ASYNC_STORAGE_LOWPRIO = true;
    /* Async storage vCPU settings */
    static constexpr uint64_t EXTRA_CPU_STACK_SIZE = 0x100000;
    static constexpr int EXTRA_CPU_ID = 16;

}
