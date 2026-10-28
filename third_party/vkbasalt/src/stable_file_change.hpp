#ifndef STABLE_FILE_CHANGE_HPP_INCLUDED
#define STABLE_FILE_CHANGE_HPP_INCLUDED

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace vkBasalt
{
    struct FileStamp
    {
        std::filesystem::file_time_type modified;
        std::uintmax_t size = 0;

        bool operator==(const FileStamp& other) const
        {
            return modified == other.modified && size == other.size;
        }

        bool operator!=(const FileStamp& other) const { return !(*this == other); }
    };

    enum class FileStampResult
    {
        Available,
        Missing,
        Error
    };

    inline FileStampResult readFileStamp(const std::filesystem::path& path, FileStamp& stamp)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            return error ? FileStampResult::Error : FileStampResult::Missing;
        stamp.modified = std::filesystem::last_write_time(path, error);
        if (error)
            return FileStampResult::Error;
        stamp.size = std::filesystem::file_size(path, error);
        return error ? FileStampResult::Error : FileStampResult::Available;
    }

    // Emits each stable stamp once, after an unchanged second sample.
    class StableFileChangeDetector
    {
    public:
        using Clock = std::chrono::steady_clock;

        void initialize(const FileStamp& stamp)
        {
            handledStamp = stamp;
            hasHandledStamp = true;
            hasCandidate = false;
        }

        bool observe(const FileStamp& stamp, Clock::time_point now)
        {
            if (hasHandledStamp && stamp == handledStamp)
            {
                hasCandidate = false;
                return false;
            }

            if (!hasCandidate || stamp != candidateStamp)
            {
                candidateStamp = stamp;
                candidateSince = now;
                hasCandidate = true;
                return false;
            }

            if (now - candidateSince < std::chrono::milliseconds(200))
                return false;

            handledStamp = stamp;
            hasHandledStamp = true;
            hasCandidate = false;
            return true;
        }

        void cancelCandidate() { hasCandidate = false; }
        bool waitingForStableSample() const { return hasCandidate; }

    private:
        FileStamp handledStamp{};
        FileStamp candidateStamp{};
        Clock::time_point candidateSince{};
        bool hasHandledStamp = false;
        bool hasCandidate = false;
    };
} // namespace vkBasalt

#endif // STABLE_FILE_CHANGE_HPP_INCLUDED
