// SPDX-License-Identifier: GPL-3.0-or-later
//
// 阈值的相互关系：生成的头文件能编译、取到值，并且档界保持有序。有人把 thresholds.json
// 改乱（比如档界颠倒）会在这里变红。
#include "config/Thresholds.h"
#include <catch2/catch_test_macros.hpp>

namespace thr = miciri::thr;

TEST_CASE("thresholds keep their bands in order")
{
    CHECK(thr::gain::tp_too_quiet_below_dbfs < thr::gain::tp_quiet_below_dbfs);
    CHECK(thr::gain::tp_quiet_below_dbfs < thr::gain::target_peak_dbfs);
    CHECK(thr::gain::target_peak_dbfs < thr::gain::tp_loud_above_dbfs);
    CHECK(thr::gain::tp_loud_above_dbfs < thr::gain::tp_too_loud_above_dbfs);
    CHECK(thr::gain::tp_too_loud_above_dbfs < 0.0);
    CHECK((thr::gain::clip_digital_threshold > 0.9 && thr::gain::clip_digital_threshold < 1.0));
    CHECK(thr::gain::true_peak_oversample_le_96k >= thr::gain::true_peak_oversample_gt_96k);

    CHECK(thr::noise::snr_bad_below_db < thr::noise::snr_good_above_db);
    CHECK(thr::reverb::dry_below_s < thr::reverb::normal_below_s);
    CHECK(thr::reverb::normal_below_s < thr::reverb::boomy_below_s);
    CHECK(thr::reverb::fit_end_db < thr::reverb::fit_start_db);
    CHECK(thr::direct_ratio::low_below_db < thr::direct_ratio::high_above_db);
    CHECK(thr::direct_ratio::direct_window_start_ms < thr::direct_ratio::direct_window_end_ms);

    CHECK(thr::no_signal::abs_dbfs < 0.0);
    CHECK(thr::no_signal::strict_zero_window_s < thr::no_signal::low_flat_window_s);
    CHECK(thr::channel_check::one_quiet_delta_db < thr::channel_check::one_sided_delta_db);

    CHECK(thr::hum::buzz_above_hz < thr::hum::whistle_min_hz);
    CHECK(thr::hum::whistle_min_hz <= thr::hum::harmonic_max_hz);
    CHECK(thr::hum::min_stable_harmonics <= thr::hum::count_harmonics_up_to);
    CHECK((thr::hum::f0_candidates_hz[0] > 0.0 && thr::hum::f0_candidates_hz[1] > thr::hum::f0_candidates_hz[0]));
    CHECK(thr::hum::slight_below_over_floor_db > thr::hum::peak_over_median_db);
    CHECK(thr::hum::whistle_min_hz < thr::hum::whistle_max_hz);
    CHECK(thr::hum::whistle_min_dbfs < 0.0);

    CHECK(thr::direction::small_difference_db < thr::direction::repeat_trigger_db);
    CHECK(thr::direction::repeat_trigger_db < thr::direction::priority_db);
}
