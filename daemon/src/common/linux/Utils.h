#include <cerrno>
#include <cstdint>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace pulsedb {

    inline float compute_percent(uint64_t part_delta, uint64_t total_delta) {
        if (total_delta == 0) {
            return 0.0f;
        }
        return (static_cast<float>(part_delta) / static_cast<float>(total_delta)) * 100.0f;
    }

    inline ssize_t read_whole_file(int fd, std::vector<char>& buf) {
        size_t total = 0;
        while (true) {
            if (total + 1 >= buf.size()) {
                buf.resize(buf.size() * 2);
            }
            ssize_t n = pread(fd, buf.data() + total, buf.size() - total - 1, total);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                else {
                    return -1;
                }
            }
            if (n == 0) {
                break; // EOF
            }
            total += n;
        }
        buf[total] = '\0';
        return total;
    }

} // namespace pulsedb
