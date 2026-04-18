// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "psa/crypto.h"

class MbedtlsMd5
{
public:
    MbedtlsMd5() = default;

    ~MbedtlsMd5()
    {
        const psa_status_t status = psa_hash_abort(&operation);
        assert(status == PSA_SUCCESS);
    }

    MbedtlsMd5(const MbedtlsMd5&) = delete;
    MbedtlsMd5& operator=(const MbedtlsMd5&) = delete;
    MbedtlsMd5(const MbedtlsMd5&&) = delete;
    MbedtlsMd5& operator=(const MbedtlsMd5&&) = delete;

    void start()
    {
        const psa_status_t status = psa_hash_setup(&operation, PSA_ALG_MD5);
        assert(status == PSA_SUCCESS);
    }

    void update(const std::string& input)
    {
        const psa_status_t status = psa_hash_update(&operation, reinterpret_cast<const unsigned char*>(input.c_str()), input.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        assert(status == PSA_SUCCESS);
    }

    void finish(std::array<unsigned char, 16>& hash)
    {
        size_t hash_len = 0;
        const psa_status_t status = psa_hash_finish(&operation, hash.data(), hash.size(), &hash_len);
        assert(status == PSA_SUCCESS);
        assert(hash_len == hash.size());
    }

private:
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
};
