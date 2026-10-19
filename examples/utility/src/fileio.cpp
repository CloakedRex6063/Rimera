#include "fileio.hpp"
#include "fstream"
#include "iterator"

#ifdef RIMERA_MAC
#include "Foundation/Foundation.hpp"
#endif

std::string FileIO::ResourcePath()
{
#ifdef RIMERA_MAC
    NS::Bundle* bundle = NS::Bundle::mainBundle();
    NS::String* path = bundle->resourcePath();
    return std::string(path->utf8String()) + "/";
#else
    return "";
#endif
}

RM::Result<std::string> FileIO::Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("Cannot open file: " + path.string());
    const std::string contents{std::istreambuf_iterator(file), std::istreambuf_iterator<char>()};
    if (file.bad()) return std::unexpected("Cannot read file: " + path.string());
    return contents;
}