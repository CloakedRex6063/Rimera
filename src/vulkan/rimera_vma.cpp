#ifdef RIMERA_WINDOWS
#include "lean_windows.hpp"
#endif
#include "volk.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1004000
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
