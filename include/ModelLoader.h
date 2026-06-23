#ifndef _MODEL_LOADER_H_
#define _MODEL_LOADER_H_

#include <string>
#include <vector>

#ifdef __EMBEDDED_MODELS__
#include "models/embedded_models.h"
#endif

class ModelLoader {
public:
    static bool loadKeysFromMemory(
        const char* keysData,
        size_t keysSize,
        std::vector<std::string>& keys
    );

    static bool isEmbeddedModelsAvailable();
};

#endif //_MODEL_LOADER_H_
