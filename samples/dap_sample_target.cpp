#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

int main() {
    volatile std::array<std::uint8_t, 8> sample_bytes = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x21, 0x00, 0x41};
    (void)sample_bytes;

    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
