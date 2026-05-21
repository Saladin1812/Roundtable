#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

int increment(int value) {
    return value + 1;
}

int main() {
    volatile std::array<std::uint8_t, 8> sample_bytes = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x21, 0x00, 0x41};
    const int                            sample_value = 42;
    (void)sample_bytes;

    const int stepped_value = increment(sample_value);
    (void)stepped_value;

    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
