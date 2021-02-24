/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */
#include "srslte/common/common.h"
#include "srslte/common/log_filter.h"
#include "srslte/common/test_common.h"
#include "srsue/hdr/stack/mac_nr/proc_ra_nr.h"

using namespace srsue;

class dummy_phy : public phy_interface_mac_nr
{
public:
  dummy_phy() {}
  void send_prach(const uint32_t prach_occasion_,
                  const int      preamble_index_,
                  const float    preamble_received_target_power_,
                  const float    ta_base_sec_ = 0.0f)
  {
    prach_occasion                 = prach_occasion_;
    preamble_index                 = preamble_index_;
    preamble_received_target_power = preamble_received_target_power_;
  }
  int tx_request(const tx_request_t& request) { return 0; }
  int set_ul_grant(std::array<uint8_t, SRSLTE_RAR_UL_GRANT_NBITS>, uint16_t rnti, srslte_rnti_type_t rnti_type)
  {
    return 0;
  }

  void get_last_send_prach(uint32_t* prach_occasion_, uint32_t* preamble_index_, int* preamble_received_target_power_)
  {
    *prach_occasion_                 = prach_occasion;
    *preamble_index_                 = preamble_index;
    *preamble_received_target_power_ = preamble_received_target_power;
  }

private:
  uint32_t prach_occasion;
  uint32_t preamble_index;
  int      preamble_received_target_power;
};

class dummy_mac : public mac_interface_proc_ra_nr
{
public:
  uint64_t get_contention_id() { return 0xdeadbeaf; }
  uint16_t get_c_rnti() { return crnti; }
  void     set_c_rnti(uint64_t c_rnti) { crnti = c_rnti; }

  bool msg3_is_transmitted() { return true; }
  void msg3_flush() {}
  void msg3_prepare() {}
  bool msg3_is_empty() { return true; }

  void msga_flush(){};

private:
  uint16_t crnti;
};

int main()
{
  srslog::init();
  auto& mac_logger = srslog::fetch_basic_logger("MAC");
  mac_logger.set_level(srslog::basic_levels::debug);
  mac_logger.set_hex_dump_max_size(-1);

  dummy_phy                     dummy_phy;
  dummy_mac                     dummy_mac;
  srslte::task_scheduler        task_sched{5, 2};
  srslte::ext_task_sched_handle ext_task_sched_h(&task_sched);

  proc_ra_nr proc_ra_nr(mac_logger);

  proc_ra_nr.init(&dummy_phy, &dummy_mac, &ext_task_sched_h);
  TESTASSERT(proc_ra_nr.is_rar_opportunity(1) == false);
  srslte::rach_nr_cfg_t rach_cfg;
  rach_cfg.powerRampingStep             = 4;
  rach_cfg.prach_ConfigurationIndex     = 16;
  rach_cfg.PreambleReceivedTargetPower  = -110;
  rach_cfg.preambleTransMax             = 7;
  rach_cfg.ra_ContentionResolutionTimer = 64;
  rach_cfg.ra_responseWindow            = 10;
  proc_ra_nr.set_config(rach_cfg);
  proc_ra_nr.start_by_rrc();

  // Test send prach parameters
  uint32_t prach_occasion                 = 0;
  uint32_t preamble_index                 = 0;
  int      preamble_received_target_power = 0;
  dummy_phy.get_last_send_prach(&prach_occasion, &preamble_index, &preamble_received_target_power);
  TESTASSERT(prach_occasion == 0);
  TESTASSERT(preamble_index == 0);
  TESTASSERT(preamble_received_target_power == -114);
  // Simulate PHY and call prach_sent (random values)
  uint32_t tti_start = 0;
  proc_ra_nr.prach_sent(tti_start, 7, 1, 0, 0);

  for (uint32_t i = tti_start; i < rach_cfg.ra_responseWindow; i++) {
    // update clock and run internal tasks
    task_sched.tic();
    task_sched.run_pending_tasks();
    bool rar_opportunity = proc_ra_nr.is_rar_opportunity(i);
    if (i < 3 + tti_start) {
      TESTASSERT(rar_opportunity == false);
    } else if (3 + tti_start > i && i < 3 + rach_cfg.ra_responseWindow) {
      TESTASSERT(rar_opportunity == true);
      TESTASSERT(proc_ra_nr.get_rar_rnti() == 0x16);
    }
  }
  mac_interface_phy_nr::mac_nr_grant_dl_t grant;
  grant.rnti               = 0x16;
  grant.tti                = rach_cfg.ra_responseWindow + tti_start + 3;
  grant.pid                = 0x0123;
  uint8_t mac_dl_rar_pdu[] = {0x40, 0x06, 0x68, 0x03, 0x21, 0x46, 0x46, 0x02, 0x00, 0x00, 0x00};
  grant.tb[0]              = srslte::make_byte_buffer();
  grant.tb[0].get()->append_bytes(mac_dl_rar_pdu, sizeof(mac_dl_rar_pdu));
  proc_ra_nr.handle_rar_pdu(grant);

  task_sched.tic();
  task_sched.run_pending_tasks();

  proc_ra_nr.pdcch_to_crnti();

  task_sched.tic();
  task_sched.run_pending_tasks();
  return SRSLTE_SUCCESS;
}