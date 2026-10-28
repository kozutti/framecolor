#include "lut_cube.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace vkBasalt
{
    LutCube::LutCube()
    {
        for (int b = 0; b < 2; ++b)
            for (int g = 0; g < 2; ++g)
                for (int r = 0; r < 2; ++r)
                    for (int c : {r, g, b, 1})
                    {
                        colorCube.push_back(static_cast<unsigned char>(c * 255));
                        colorCube16.push_back(static_cast<uint16_t>(c * 65535));
                    }
    }
    LutCube::LutCube(const std::string& file) : LutCube()
    {
        try
        {
            std::ifstream input(file);
            if (!input) throw std::runtime_error("Cannot open LUT");
            int parsedSize = 0;
            bool seenMin = false, seenMax = false;
            std::vector<std::array<float, 3>> rows;
            std::string line;
            while (std::getline(input, line))
            {
                if (line.size() > 4096) throw std::runtime_error("LUT line too long");
                const auto comment = line.find('#');
                if (comment != std::string::npos) line.erase(comment);
                std::istringstream stream(line);
                std::string key, extra;
                if (!(stream >> key)) continue;
                if (key == "TITLE") continue;
                if (key == "LUT_3D_SIZE")
                {
                    if (parsedSize || !(stream >> parsedSize) || parsedSize < 2 || parsedSize > 65 || stream >> extra)
                        throw std::runtime_error("LUT size must be 2..65, once only");
                    continue;
                }
                if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX")
                {
                    bool& seen = key == "DOMAIN_MIN" ? seenMin : seenMax;
                    float a, b, c, expected = key == "DOMAIN_MIN" ? 0.0f : 1.0f;
                    if (seen || !(stream >> a >> b >> c) || a != expected || b != expected || c != expected || stream >> extra)
                        throw std::runtime_error("Only domain 0..1 is supported");
                    seen = true;
                    continue;
                }
                if (!parsedSize || rows.size() >= static_cast<size_t>(parsedSize * parsedSize * parsedSize))
                    throw std::runtime_error("Unexpected LUT data");
                std::istringstream values(line);
                std::array<float, 3> row;
                if (!(values >> row[0] >> row[1] >> row[2]) || values >> extra)
                    throw std::runtime_error("Expected three LUT values");
                for (float value : row)
                    if (!std::isfinite(value) || value < 0 || value > 1)
                        throw std::runtime_error("LUT values must be finite and within 0..1");
                rows.push_back(row);
            }
            if (!parsedSize || rows.size() != static_cast<size_t>(parsedSize * parsedSize * parsedSize))
                throw std::runtime_error("Incomplete LUT");
            std::vector<unsigned char> bytes;
            std::vector<uint16_t> words;
            for (const auto& row : rows)
            {
                for (float value : row)
                {
                    bytes.push_back(static_cast<unsigned char>(std::lround(value * 255)));
                    words.push_back(static_cast<uint16_t>(std::lround(value * 65535)));
                }
                bytes.push_back(255);
                words.push_back(65535);
            }
            size = parsedSize;
            colorCube = std::move(bytes);
            colorCube16 = std::move(words);
            valid = true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
    }
}
