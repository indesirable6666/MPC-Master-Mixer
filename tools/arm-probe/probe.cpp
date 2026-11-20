#include <iostream>
#include <string>
#include <vector>

int main() {
    const std::string target = "MPC Live II";
    const std::vector<int> checks{32, 28, 10};

    std::cout << "MPC Master Mixer: C++ runtime probe\n";
    std::cout << "Target: " << target << "\n";
    std::cout << "Runtime checks: ";
    for (std::size_t i = 0; i < checks.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << checks[i];
    }
    std::cout << "\nNo screen, touch, MIDI, or audio access.\n";
    return 0;
}
