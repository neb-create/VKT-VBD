#include "resource-helper.h"
#include <fstream>

using namespace std;

vector<char> readFile(const std::string& fileName) {
    std::ifstream file(fileName,
        std::ios::ate | // start from end of file to easily get filesize
        std::ios::binary // read as binary
    );

    if (!file.is_open()) {
        throw std::runtime_error("Couldn't open file");
    }

    vector<char> buffer(file.tellg()); // cursor pos is size
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    file.close();

    return buffer;
}