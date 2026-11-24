// The entry point that makes modules/pna-aes.so a loadable cipher module.
#include "pna_aes.h"

extern "C" __attribute__((visibility("default"))) const pna_cipher_module* pna_cipher_module_v1(void) {
    return pna_aes_module();
}
