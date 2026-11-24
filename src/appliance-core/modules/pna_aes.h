// The appliance's own cipher as a cipher-slot module (modules/pna_aes.cpp).
#pragma once

#include "../sdk/pna_cipher.h"

extern "C" const pna_cipher_module* pna_aes_module(void);
