// Test to understand AudioParameterChoice index behavior
#include <iostream>

int main() {
    // ComboBox IDs start at 1
    // So when we do addItemList({"Room", "Hall", "Plate", "Early"}, 1)
    // Room gets ID 1, Hall gets ID 2, Plate gets ID 3, Early gets ID 4

    // But AudioParameterChoice getIndex() returns 0-based indices
    // So Room = 0, Hall = 1, Plate = 2, Early = 3

    std::cout << "ComboBox IDs (start at 1):" << std::endl;
    std::cout << "  Room: ID 1" << std::endl;
    std::cout << "  Hall: ID 2" << std::endl;
    std::cout << "  Plate: ID 3" << std::endl;
    std::cout << "  Early: ID 4" << std::endl;

    std::cout << "\nAudioParameterChoice indices (0-based):" << std::endl;
    std::cout << "  Room: index 0" << std::endl;
    std::cout << "  Hall: index 1" << std::endl;
    std::cout << "  Plate: index 2" << std::endl;
    std::cout << "  Early: index 3" << std::endl;

    std::cout << "\nWhen user selects from ComboBox:" << std::endl;
    for (int comboId = 1; comboId <= 4; ++comboId) {
        int paramIndex = comboId - 1;
        std::cout << "  ComboBox ID " << comboId << " -> Parameter index " << paramIndex << std::endl;
    }

    return 0;
}