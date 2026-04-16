/*
   Copyright 2017 Christian Taedcke <hacking@taedcke.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
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
