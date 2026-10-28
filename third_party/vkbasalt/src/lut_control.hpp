#ifndef LUT_CONTROL_HPP_INCLUDED
#define LUT_CONTROL_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace vkBasalt
{
    struct LutSelection
    {
        bool enabled = false;
        std::string file;
        bool valid = false;
        std::string error;
    };

    inline bool isSafeLutBasename(const std::string& name)
    {
        if (name.empty() || name == "." || name == "..")
            return false;
        for (unsigned char character : name)
            if (character == '/' || character == '\\' || character < 0x20 || character == 0x7f)
                return false;
        return true;
    }

    // A missing file field represents an empty selection. A present but empty
    // file field is rejected so an incomplete or truncated selection is never
    // interpreted as a request to load a path.
    inline LutSelection parseLutSelection(const std::string& text)
    {
        LutSelection result;
        bool hasEnabled = false;
        bool hasFile = false;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (line.compare(0, 8, "enabled ") == 0)
            {
                if (hasEnabled || (line != "enabled 0" && line != "enabled 1"))
                {
                    result.error = "invalid enabled value";
                    return result;
                }
                result.enabled = line == "enabled 1";
                hasEnabled = true;
            }
            else if (line.compare(0, 5, "file ") == 0)
            {
                if (hasFile)
                {
                    result.error = "duplicate file";
                    return result;
                }
                result.file = line.substr(5);
                if (!isSafeLutBasename(result.file))
                {
                    result.error = "invalid file name";
                    return result;
                }
                hasFile = true;
            }
            else
            {
                result.error = "invalid selection line";
                return result;
            }
        }
        if (!hasEnabled)
        {
            result.error = "missing enabled value";
            return result;
        }
        result.valid = true;
        return result;
    }

    enum class LutStatusState
    {
        Active,
        Off,
        Error,
        Unsupported
    };

    struct LutStatus
    {
        LutStatusState state = LutStatusState::Off;
        std::string file;
        std::string message;

        bool operator==(const LutStatus& other) const
        {
            return state == other.state && file == other.file && message == other.message;
        }

        bool operator!=(const LutStatus& other) const { return !(*this == other); }
    };

    inline const char* lutStatusStateName(LutStatusState state)
    {
        switch (state)
        {
            case LutStatusState::Active: return "active";
            case LutStatusState::Off: return "off";
            case LutStatusState::Error: return "error";
            case LutStatusState::Unsupported: return "unsupported";
        }
        return "error";
    }

    inline bool parseLutStatusState(const std::string& name, LutStatusState& state)
    {
        if (name == "active") state = LutStatusState::Active;
        else if (name == "off") state = LutStatusState::Off;
        else if (name == "error") state = LutStatusState::Error;
        else if (name == "unsupported") state = LutStatusState::Unsupported;
        else return false;
        return true;
    }

    inline std::string formatLutStatus(const LutStatus& status)
    {
        const std::string file = isSafeLutBasename(status.file) ? status.file : "-";
        std::string message;
        for (unsigned char character : status.message)
        {
            if (message.size() >= 96)
                break;
            message.push_back(character >= 0x20 && character <= 0x7e ? static_cast<char>(character) : '?');
        }
        if (message.empty())
            message = "-";
        return std::string("state ") + lutStatusStateName(status.state) + "\nfile " + file +
               "\nmessage " + message + "\n";
    }

    inline bool parseLutStatus(const std::string& text, LutStatus& status)
    {
        bool hasState = false, hasFile = false, hasMessage = false;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0, 6, "state ") == 0)
            {
                if (hasState || !parseLutStatusState(line.substr(6), status.state))
                    return false;
                hasState = true;
            }
            else if (line.compare(0, 5, "file ") == 0)
            {
                if (hasFile)
                    return false;
                status.file = line.substr(5);
                if (status.file == "-")
                    status.file.clear();
                else if (!isSafeLutBasename(status.file))
                    return false;
                hasFile = true;
            }
            else if (line.compare(0, 8, "message ") == 0)
            {
                if (hasMessage)
                    return false;
                status.message = line.substr(8);
                hasMessage = true;
            }
            else if (!line.empty())
                return false;
        }
        return hasState && hasFile && hasMessage;
    }

    struct LutRgbCube
    {
        int size = 0;
        std::vector<std::array<float, 3>> values;
    };

    inline LutRgbCube makeIdentityLutCube(int size)
    {
        LutRgbCube cube;
        if (size < 2)
            return cube;
        cube.size = size;
        cube.values.reserve(static_cast<std::size_t>(size) * size * size);
        for (int blue = 0; blue < size; ++blue)
            for (int green = 0; green < size; ++green)
                for (int red = 0; red < size; ++red)
                    cube.values.push_back({{static_cast<float>(red) / (size - 1),
                                            static_cast<float>(green) / (size - 1),
                                            static_cast<float>(blue) / (size - 1)}});
        return cube;
    }

    inline LutRgbCube resampleLutCube(const LutRgbCube& source, int targetSize)
    {
        LutRgbCube result;
        if (source.size < 2 || targetSize < 2 ||
            source.values.size() != static_cast<std::size_t>(source.size) * source.size * source.size)
            return result;

        result.size = targetSize;
        result.values.resize(static_cast<std::size_t>(targetSize) * targetSize * targetSize);
        const auto sourceIndex = [&source](int red, int green, int blue) {
            return (static_cast<std::size_t>(blue) * source.size + green) * source.size + red;
        };
        for (int blue = 0; blue < targetSize; ++blue)
            for (int green = 0; green < targetSize; ++green)
                for (int red = 0; red < targetSize; ++red)
                {
                    const float sourceRed = static_cast<float>(red) * (source.size - 1) / (targetSize - 1);
                    const float sourceGreen = static_cast<float>(green) * (source.size - 1) / (targetSize - 1);
                    const float sourceBlue = static_cast<float>(blue) * (source.size - 1) / (targetSize - 1);
                    const int r0 = static_cast<int>(std::floor(sourceRed));
                    const int g0 = static_cast<int>(std::floor(sourceGreen));
                    const int b0 = static_cast<int>(std::floor(sourceBlue));
                    const int r1 = std::min(r0 + 1, source.size - 1);
                    const int g1 = std::min(g0 + 1, source.size - 1);
                    const int b1 = std::min(b0 + 1, source.size - 1);
                    const float fr = sourceRed - r0;
                    const float fg = sourceGreen - g0;
                    const float fb = sourceBlue - b0;
                    std::array<float, 3> value{};
                    for (std::size_t channel = 0; channel < value.size(); ++channel)
                    {
                        const float c000 = source.values[sourceIndex(r0, g0, b0)][channel];
                        const float c100 = source.values[sourceIndex(r1, g0, b0)][channel];
                        const float c010 = source.values[sourceIndex(r0, g1, b0)][channel];
                        const float c110 = source.values[sourceIndex(r1, g1, b0)][channel];
                        const float c001 = source.values[sourceIndex(r0, g0, b1)][channel];
                        const float c101 = source.values[sourceIndex(r1, g0, b1)][channel];
                        const float c011 = source.values[sourceIndex(r0, g1, b1)][channel];
                        const float c111 = source.values[sourceIndex(r1, g1, b1)][channel];
                        const float lowBlue = (c000 * (1.0f - fr) + c100 * fr) * (1.0f - fg) +
                                              (c010 * (1.0f - fr) + c110 * fr) * fg;
                        const float highBlue = (c001 * (1.0f - fr) + c101 * fr) * (1.0f - fg) +
                                               (c011 * (1.0f - fr) + c111 * fr) * fg;
                        value[channel] = lowBlue * (1.0f - fb) + highBlue * fb;
                    }
                    result.values[(static_cast<std::size_t>(blue) * targetSize + green) * targetSize + red] = value;
                }
        return result;
    }
} // namespace vkBasalt

#endif // LUT_CONTROL_HPP_INCLUDED
