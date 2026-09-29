#pragma once

#ifdef RIMERA_MAC
#include "Metal/Metal.hpp"
#include "Metal/MTL4AccelerationStructure.hpp"
#include "QuartzCore/QuartzCore.hpp"
#include "Foundation/Foundation.hpp"
#include "TargetConditionals.h"
#elifdef RIMERA_WINDOWS
#include "lean_windows.hpp"
#include "wrl/client.h"
using namespace Microsoft::WRL;

#include "directx/d3d12.h"
#include "dxgi1_6.h"
#include "D3D12MemAlloc.h"
#endif

#include "memory"
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

