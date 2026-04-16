// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2026 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include <string>

struct Registration
{
    std::string realm;
    std::string nonce;
    std::string response;
    bool proxy_auth { false };
};
