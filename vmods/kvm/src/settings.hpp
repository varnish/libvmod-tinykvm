#pragma once

namespace kvm
{
    static constexpr uint16_t DEBUG_PORT    = 2159;
    static constexpr float    DEBUG_TIMEOUT = 120.0f;

    static constexpr size_t MAIN_STACK_SIZE = 1UL << 22; /* 4MB */
    static constexpr size_t MAIN_MEMORY_SIZE = 256; /* 256MB */
    static constexpr size_t SHARED_MEMORY_SIZE = 0; /* No shared mem */
    static constexpr float  STARTUP_TIMEOUT = 16.0f;

    static constexpr uint32_t RESV_QUEUE_TIMEOUT = 60; /* Seconds */
    static constexpr size_t REQUEST_MEMORY_SIZE = 64; /* 64MB */
    static constexpr int    REQUEST_VM_NICE = 0;
    static constexpr float  REQUEST_VM_TIMEOUT = 8.0f;
    static constexpr float  STREAM_HANDLING_TIMEOUT = 2.0f;
    static constexpr float  ERROR_HANDLING_TIMEOUT = 1.0f;

    /* Serialized storage VM access */
    static constexpr int   STORAGE_VM_NICE = 0;
    static constexpr float STORAGE_TIMEOUT = 10.0f;
    static constexpr float STORAGE_CLEANUP_TIMEOUT = 1.0f;
    static constexpr float STORAGE_DESERIALIZE_TIMEOUT = 2.0f;
    static constexpr int   STORAGE_TASK_MAX_TIMERS = 15;
    /* Async storage VM access */
    static constexpr float ASYNC_STORAGE_TIMEOUT = 15.0f;
    static constexpr int   ASYNC_STORAGE_NICE = 15;
    static constexpr bool  ASYNC_STORAGE_LOWPRIO = true;
    /* Async storage vCPU settings */
    static constexpr uint64_t EXTRA_CPU_STACK_SIZE = 0x100000;
    static constexpr int EXTRA_CPU_ID = 16;

}
