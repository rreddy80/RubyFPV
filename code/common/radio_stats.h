#pragma once
#include "../base/base.h"
#include "../base/shared_mem.h"

#ifdef __cplusplus
extern "C" {
#endif  

void shared_mem_radio_stats_rx_hist_reset(shared_mem_radio_stats_rx_hist* pStats);
void shared_mem_radio_stats_rx_hist_update(shared_mem_radio_stats_rx_hist* pStats, int iInterfaceIndex, u8* pPacket, u32 uTimeNow);

void radio_stats_interfaces_rx_graph_reset(shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, int iGraphSliceDuration);

void radio_stats_reset(shared_mem_radio_stats* pSMRS, int graphRefreshInterval);
void radio_stats_set_graph_refresh_interval(shared_mem_radio_stats* pSMRS, int graphRefreshInterval);

void radio_stats_enable_history_monitor(int iEnable);

void radio_stats_log_info(shared_mem_radio_stats* pSMRS, u32 uTimeNow);
int  radio_stats_periodic_update(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow);

void radio_stats_set_radio_link_rt_delay(shared_mem_radio_stats* pSMRS, int iLocalRadioLink, u32 delay, u32 timeNow);
void radio_stats_set_commands_rt_delay(shared_mem_radio_stats* pSMRS, u32 delay);
void radio_stats_set_tx_card_for_radio_link(shared_mem_radio_stats* pSMRS, int iLocalRadioLink, int iTxCard);
void radio_stats_set_card_current_frequency(shared_mem_radio_stats* pSMRS, int iRadioInterface, u32 freqKhz);
void radio_stats_set_bad_data_on_current_rx_interval(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, int iRadioInterface);

int  radio_stats_update_on_new_radio_packet_received(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow, int iInterfaceIndex, u8* pPacketBuffer, int iPacketLength, int iIsShortPacket, int iIsVideo, int iDataIsOk);
int  radio_stats_update_on_unique_packet_received(shared_mem_radio_stats* pSMRS, shared_mem_radio_stats_interfaces_rx_graph* pSMRXStats, u32 timeNow, int iInterfaceIndex, u8* pPacketBuffer, int iPacketLength);
void radio_stats_update_on_packet_sent_on_radio_interface(shared_mem_radio_stats* pSMRS, u32 timeNow, int interfaceIndex, int iPacketLength);
void radio_stats_update_on_packet_sent_on_radio_link(shared_mem_radio_stats* pSMRS, u32 timeNow, int iLocalLinkIndex, int iStreamIndex, int iPacketLength, int iChainedCount);
void radio_stats_update_on_packet_sent_for_radio_stream(shared_mem_radio_stats* pSMRS, u32 timeNow, u32 uVehicleId, int iStreamIndex, int iPacketLength);

void radio_controller_links_stats_reset(t_packet_data_controller_link_stats* pControllerStats);
void radio_controller_links_stats_periodic_update(t_packet_data_controller_link_stats* pControllerStats, u32 timeNow);

#ifdef __cplusplus
}  
#endif
