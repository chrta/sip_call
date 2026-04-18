// SPDX-License-Identifier: AGPL-3.0-or-later
/*
   Copyright 2017-2021 Christian Taedcke <hacking@taedcke.com>
 */

#pragma once

#include "boost/sml.hpp"

namespace sml = boost::sml;

struct Logger
{
    template <class SM, class TEvent>
    void log_process_event(const TEvent& /*unused*/)
    {
        ESP_LOGI(TAG, "[%s][process_event] %s\n", sml::aux::get_type_name<SM>(), sml::aux::get_type_name<TEvent>());
    }

    template <class SM, class TGuard, class TEvent>
    void log_guard(const TGuard& /*unused*/, const TEvent& /*unused*/, bool result)
    {
        ESP_LOGI(TAG, "[%s][guard] %s %s %s\n", sml::aux::get_type_name<SM>(), sml::aux::get_type_name<TGuard>(),
            sml::aux::get_type_name<TEvent>(), (result ? "[OK]" : "[Reject]"));
    }

    template <class SM, class TAction, class TEvent>
    void log_action(const TAction& /*unused*/, const TEvent& /*unused*/)
    {
        ESP_LOGI(TAG, "[%s][action] %s %s\n", sml::aux::get_type_name<SM>(), sml::aux::get_type_name<TAction>(),
            sml::aux::get_type_name<TEvent>());
    }

    template <class SM, class TSrcState, class TDstState>
    void log_state_change(const TSrcState& src, const TDstState& dst)
    {
        ESP_LOGI(TAG, "[%s][transition] %s -> %s\n", sml::aux::get_type_name<SM>(), src.c_str(), dst.c_str());
    }

private:
    static constexpr const char* TAG = "SipSm";
};
