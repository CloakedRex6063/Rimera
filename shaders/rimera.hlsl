#pragma once

#ifdef __spirv__
#define PER_PRIMITIVE [[vk::ext_decorate(5271)]]
#define MESH_FRAGMENT [[vk::ext_extension("SPV_EXT_mesh_shader")]] [[vk::ext_capability(5283)]]
#else
#define PER_PRIMITIVE
#define MESH_FRAGMENT
#endif
