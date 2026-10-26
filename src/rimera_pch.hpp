#pragma once

#ifdef RIMERA_WINDOWS
#include "lean_windows.hpp"
#include "wrl/client.h"
using namespace Microsoft::WRL;
#endif

#ifdef RIMERA_METAL
#include "Metal/Metal.hpp"
#include "Metal/MTL4AccelerationStructure.hpp"
#include "QuartzCore/QuartzCore.hpp"
#include "Foundation/Foundation.hpp"
#include "TargetConditionals.h"
#ifdef __OBJC__
#include "Metal/Metal.h"
#include "QuartzCore/QuartzCore.h"
#include "Foundation/Foundation.h"
#include "AppKit/AppKit.h"
#endif

#elifdef RIMERA_D3D12
#include "directx/d3d12.h"
#include "dxgi1_6.h"
#include "D3D12MemAlloc.h"

#elifdef RIMERA_VULKAN
#include "volk.h"
#include "vk_mem_alloc.h"
#endif

#include "memory"
#include "cstdint"
#include "cstdio"
#include "cstdlib"
#include "numeric"
#include "span"
#include "expected"
#include "cstring"
#include "array"
#include "string_view"
#include "string"
#include "filesystem"
namespace fs = std::filesystem;
#include "vector"
#include "iostream"
#include "fstream"
#include "ranges"
#include "algorithm"