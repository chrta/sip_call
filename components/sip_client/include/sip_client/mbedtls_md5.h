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

#include "mbedtls/md5.h"

class MbedtlsMd5
{
public:
    MbedtlsMd5()
    {
        mbedtls_md5_init(&m_ctx);
    }

    ~MbedtlsMd5()
    {
        mbedtls_md5_free(&m_ctx);
    }

    MbedtlsMd5(const MbedtlsMd5&) = delete;
    MbedtlsMd5& operator=(const MbedtlsMd5&) = delete;
    MbedtlsMd5(const MbedtlsMd5&&) = delete;
    MbedtlsMd5& operator=(const MbedtlsMd5&&) = delete;

    void start()
    {
        mbedtls_md5_starts(&m_ctx);
    }

    void update(const std::string& input)
    {
        mbedtls_md5_update(&m_ctx, reinterpret_cast<const unsigned char*>(input.c_str()), input.size());
    }

    void finish(std::array<unsigned char, 16>& hash)
    {
        mbedtls_md5_finish(&m_ctx, hash.data());
    }

private:
    mbedtls_md5_context m_ctx {};
};
