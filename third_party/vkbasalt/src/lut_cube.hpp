#ifndef LUT_CUBE_HPP_INCLUDED
#define LUT_CUBE_HPP_INCLUDED
#include <cstdint>
#include <string>
#include <vector>
namespace vkBasalt
{
    // Strict domain 0..1 reader. Invalid files become an identity LUT.
    class LutCube
    {
    public:
        std::vector<unsigned char> colorCube;
        std::vector<uint16_t> colorCube16;
        int size = 2;
        bool valid = false;
        std::string error;
        explicit LutCube(const std::string& file);
        LutCube();
    };
}
#endif
