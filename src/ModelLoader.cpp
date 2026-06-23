#include "ModelLoader.h"
#include <sstream>

bool ModelLoader::loadKeysFromMemory(
    const char* keysData,
    size_t keysSize,
    std::vector<std::string>& keys
) {
    if (!keysData || keysSize == 0) return false;

    std::string content(keysData, keysSize);

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty()) {
            keys.push_back(line);
        }
    }

    return !keys.empty();
}

bool ModelLoader::isEmbeddedModelsAvailable() {
#ifdef __EMBEDDED_MODELS__
    return true;
#else
    return false;
#endif
}
