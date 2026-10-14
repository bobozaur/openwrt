# Native AP/STA channel metadata

Negotiated channel definitions retain the center-channel index returned by
`ieee80211_freq_to_chan()` on every supported band, including 2.4 GHz. The
frequency-info fallback uses the same native conversion for HT20/40, with
center frequency equal to the primary frequency plus ten MHz per secondary
channel offset, as in `hostapd_set_freq_params()`. Both representations now
agree with hostapd's actual channel-switch callback.

The exact channel/beaconing permission checks remain active. This metadata
correction does not relax channel geometry or regulatory checks, change the
80/160/320 MHz mapping, or alter security and scan timing.

Pausing an AP requests native peer disconnection while station keys still exist,
then retires hostapd station state before stopping the driver. Peers establish
fresh associations when the AP resumes; a paused AP does not retain station
records whose driver keys and peer identifiers have been removed. Native PMF
handling remains responsible for protected management frames.

# Connection scan watchdog recovery

An nl80211 watchdog abort remains an incomplete scan: its cached BSS entries
cannot drive network selection. When the matching native aborted completion
confirms that watchdog request, an enabled, still-connecting owner schedules a
fresh scan through the normal one-second retry path. The real completion also
establishes scan-event support for the existing learned-event timeout; neither
the initial nor learned timeout budget is changed.

Explicit `ABORT_SCAN`, manual or external scans, special scan handlers, and
removed, disabled or disconnected owners do not gain this retry. `DISCONNECT`
continues to cancel pending scans and request abortion of an active scan. A
retry is observable at the normal INFO level as `CTRL-EVENT-SCAN-FAILED` with
`retry=1 reason=watchdog`; `ret` contains the platform's negative `ETIMEDOUT`.
Driver provenance is cleared on new scan attempts, failed abortion, explicit
cancellation, completion and interface/link retirement.

# Supplicant WPS credential events

The `wps_credentials` ubus event retains `ifname`, `ssid`, `encryption` and
`key`. Its `processed` flag is true only after the supplicant has accepted the
native network profile, applied its security adjustments, and completed any
requested configuration-file write. Accepted events also contain `network_id`
and the native configuration strings `proto`, `key_mgmt` and `pairwise`.

With `wps_cred_processing=1`, credentials are handed to an external handler and
`processed` is false; native profile fields are absent. An event does not imply
that credentials have been committed to UCI or that association has completed.
Consumers must preserve the accepted security policy and their own configuration
ownership when implementing persistence.

# EAP peer identity with mbedTLS

The mbedTLS backend delegates an explicitly configured certificate-name policy
or `hash://server/sha256/` leaf pin to its existing EAP certificate verifier. This
allows suffix and alternative-name policies without substituting a single TLS
hostname. Missing peer-identity policy still retains mbedTLS's refusal to verify
an unspecified hostname. The native policy is unchanged: name constraints keep
normal certificate-chain checks; a configured leaf pin uses the existing pin
branch and does not additionally apply the separate name constraints.

# UBUS methods - hostapd

## bss_mgmt_enable
Enable 802.11k/v features.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| neighbor_report | bool | no | enable 802.11k neighbor reports |
| beacon_report | bool | no | enable 802.11k beacon reports |
| link_measurements | bool | no | enable 802.11k link measurements |
| bss_transition | bool | no | enable 802.11v BSS transition support |

### example
`ubus call hostapd.wl5-fb bss_mgmt_enable '{ "neighbor_report": true, "beacon_report": true, "link_measurements": true, "bss_transition": true
}'`


## bss_transition_request
Initiate an 802.11v transition request.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| addr | string | yes | client MAC address |
| disassociation_imminent | bool | no | set Disassociation Imminent bit |
| disassociation_timer | int32 | no | disassociate client if it doesn't roam after this time |
| validity_period | int32 | no | validity of the BSS Transition Candiate List |
| neighbors | array | no | BSS Transition Candidate List |
| abridged | bool | no | prefer APs in the BSS Transition Candidate List |
| dialog_token | int32 | no | request/report identifier, 1–255; defaults to 1 when omitted |
| mbo_reason | int32 | no | MBO Transition Reason Code Attribute |
| cell_pref | int32 | no | MBO Cellular Data Connection Preference Attribute |
| reassoc_delay | int32 | no | MBO Re-association retry delay |

### example
`ubus call hostapd.wl5-fb bss_transition_request '{ "addr": "68:2F:67:8B:98:ED", "disassociation_imminent": false, "disassociation_timer": 0, "validity_period": 30, "neighbors": ["b6a7b9cbeebabf5900008064090603026a00"], "abridged": 1 }'`


## config_add
Dynamically load a BSS configuration from a file. This is used by netifd's mac80211 support script to configure BSSes on multiple PHYs in a single hostapd instance.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| iface | string | yes | WiFi interface name |
| config | string | yes | path to hostapd config file |


## config_remove
Dynamically remove a BSS configuration.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| iface | string | yes | WiFi interface name |


## del_client
Kick a client off the network.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| addr | string | yes | client MAC address |
| reason | int32 | no | 802.11 reason code |
| deauth | bool | no | deauthenticates client instead of disassociating |
| ban_time | int32 | no | ban client for N milliseconds |

### example
`ubus call hostapd.wl5-fb del_client '{ "addr": "68:2f:67:8b:98:ed", "reason": 5, "deauth": true, "ban_time": 10000 }'`


## get_clients
Show associated clients.

### example
`ubus call hostapd.wl5-fb get_clients`

### output
```json
{
        "freq": 5260,
        "clients": {
                "68:2f:67:8b:98:ed": {
                        "auth": true,
                        "assoc": true,
                        "authorized": true,
                        "preauth": false,
                        "wds": false,
                        "wmm": true,
                        "ht": true,
                        "vht": true,
                        "he": false,
                        "wps": false,
                        "mfp": true,
                        "rrm": [
                                0,
                                0,
                                0,
                                0,
                                0
                        ],
                        "extended_capabilities": [
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                64
                        ],
                        "aid": 3,
                        "signature": "wifi4|probe:0,1,45,127,107,191,221(0017f2,10),221(001018,2),htcap:006f,htagg:1b,htmcs:0000ffff,vhtcap:0f825832,vhtrxmcs:0000ffea,vhttxmcs:0000ffea,extcap:0000008000000040|assoc:0,1,33,36,48,45,127,191,221(0017f2,10),221(001018,2),221(0050f2,2),htcap:006f,htagg:1b,htmcs:0000ffff,vhtcap:0f825832,vhtrxmcs:0000ffea,vhttxmcs:0000ffea,txpow:14f9,extcap:0000000000000040",
                        "bytes": {
                                "rx": 1933667,
                                "tx": 746805
                        },
                        "airtime": {
                                "rx": 208863,
                                "tx": 9037883
                        },
                        "packets": {
                                "rx": 3587,
                                "tx": 2185
                        },
                        "rate": {
                                "rx": 866700,
                                "tx": 866700
                        },
                        "signal": -50,
                        "capabilities": {
                                "vht": {
                                        "su_beamformee": true,
                                        "mu_beamformee": false,
                                        "mcs_map": {
                                                "rx": {
                                                        "1ss": 9,
                                                        "2ss": 9,
                                                        "3ss": 9,
                                                        "4ss": -1,
                                                        "5ss": -1,
                                                        "6ss": -1,
                                                        "7ss": -1,
                                                        "8ss": -1
                                                },
                                                "tx": {
                                                        "1ss": 9,
                                                        "2ss": 9,
                                                        "3ss": 9,
                                                        "4ss": -1,
                                                        "5ss": -1,
                                                        "6ss": -1,
                                                        "7ss": -1,
                                                        "8ss": -1
                                                }
                                        }
                                }
                        }
                }
        }
}
```


## get_features
Show HT/VHT support.

### example
`ubus call hostapd.wl5-fb get_features`

### output
```json
{
        "ht_supported": true,
        "vht_supported": true
}
```


## get_status
Get BSS status.

### example
`ubus call hostapd.wl5-fb get_status`

### output
```json
{
        "status": "ENABLED",
        "bssid": "b6:a7:b9:cb:ee:bc",
        "ssid": "fb",
        "freq": 5260,
        "channel": 52,
        "op_class": 128,
        "beacon_interval": 100,
        "phy": "wl5-lan",
        "rrm": {
                "neighbor_report_tx": 0
        },
        "wnm": {
                "bss_transition_query_rx": 0,
                "bss_transition_request_tx": 0,
                "bss_transition_response_rx": 0
        },
        "airtime": {
                "time": 259561738,
                "time_busy": 2844249,
                "utilization": 0
        },
        "dfs": {
                "cac_seconds": 60,
                "cac_active": false,
                "cac_seconds_left": 0
        }
}
```


## link_measurement_req
Initiate an 802.11k Link Measurement Request.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| addr | string | yes | client MAC address |
| tx-power-used | int32 | no | transmit power used to transmit the Link Measurement Request frame |
| tx-power-max | int32 | no | upper limit of transmit power to be used by the client |


## list_bans
List banned clients.

### example
`ubus call hostapd.wl5-fb list_bans`

### output
```json
{
        "clients": [
                "68:2f:67:8b:98:ed"
        ]
}
```


## notify_response
When enabled, hostapd will send a ubus notification and wait for a response before responding to various requests. This is used by e.g. usteer to make it possible to ignore probe requests.

:warning: enabling this will cause hostapd to stop responding to probe requests unless a ubus subscriber responds to the ubus notifications.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| notify_response | int32 | yes | disable (0) or enable (!0) |

### example
`ubus call hostapd.wl5-fb notify_response '{ "notify_response": 1 }'`

## reload
Reload BSS configuration.

:warning: this can cause problems for certain configurations:

```
Mon May 16 16:09:08 2022 daemon.warn hostapd: Failed to check if DFS is required; ret=-1
Mon May 16 16:09:08 2022 daemon.warn hostapd: Failed to check if DFS is required; ret=-1
Mon May 16 16:09:08 2022 daemon.err hostapd: Wrong coupling between HT and VHT/HE channel setting
```

### example
`ubus call hostapd.wl5-fb reload`


## rrm_beacon_req
Send a Beacon Measurement Request to a client.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| addr | string | yes | client MAC address |
| op_class | int32 | yes | the Regulatory Class for which this Measurement Request applies |
| channel | int32 | yes | channel to measure |
| duration | int32 | yes | compile Beacon Measurement Report after N TU |
| mode | int32 | yes | mode to be used for measurement (0: passive, 1: active, 2: beacon table) |
| bssid | string | no | filter BSSes in Beacon Measurement Report by BSSID |
| ssid | string | no | filter BSSes in Beacon Measurement Report by SSID|


## rrm_nr_get_own
Show Neighbor Report Element for this BSS.

### example
`ubus call hostapd.wl5-fb rrm_nr_get_own`

### output
```json
{
        "value": [
                "b6:a7:b9:cb:ee:bc",
                "fb",
                "b6a7b9cbeebcaf5900008095090603029b00"
        ]
}
```


## rrm_nr_list
Show Neighbor Report Elements for other BSSes in this ESS.

### example
`ubus call hostapd.wl5-fb rrm_nr_list`

### output
```json
{
        "list": [
                [
                        "b6:a7:b9:cb:ee:ba",
                        "fb",
                        "b6a7b9cbeebabf5900008064090603026a00"
                ]
        ]
}
```

## rrm_nr_set
Set the Neighbor Report Elements. An element for the node on which this command is executed will always be added.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| list | array | yes | array of Neighbor Report Elements in the format of the rrm_nr_list output |

### example
`ubus call hostapd.wl5-fb rrm_nr_set '{ "list": [ [ "b6:a7:b9:cb:ee:ba", "fb", "b6a7b9cbeebabf5900008064090603026a00" ] ] }'`


## set_vendor_elements
Configure Vendor-specific Information Elements for BSS.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| vendor_elements | string | yes | Vendor-specific Information Elements as hex string |

### example
`ubus call hostapd.wl5-fb set_vendor_elements '{ "vendor_elements": "dd054857dd6662" }'`


## switch_chan
Initiate a channel switch.

:warning: trying to switch to the channel that is currently in use will fail: `Command failed: Operation not supported`

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| freq | int32 | yes | frequency in MHz to switch to |
| bcn_count | int32 | no | count in Beacon frames (TBTT) to perform the switch |
| center_freq1 | int32 | no | segment 0 center frequency in MHz (valid for HT and VHT) |
| center_freq2 | int32 | no | segment 1 center frequency in MHz (valid only for 80 MHz channel width and an 80+80 channel) |
| bandwidth | int32 | no | channel width to use |
| sec_channel_offset| int32 | no | secondary channel offset for HT40 (0 = disabled, 1 = HT40+, -1 = HT40-) |
| ht | bool | no | enable 802.11n |
| vht | bool | no | enable 802.11ac |
| he | bool | no | enable 802.11ax |
| block_tx | bool | no | block transmission during CSA period |
| csa_force | bool | no | restart the interface in case the CSA fails |

## example
`ubus call hostapd.wl5-fb switch_chan '{ "freq": 5180, "bcn_count": 10, "center_freq1": 5210, "bandwidth": 80, "he": 1, "block_tx": 1, "csa_force": 0 }'`


## update_airtime
Set dynamic airtime weight for client.

### arguments
| Name | Type | Required | Description |
|---|---|---|---|
| sta | string | yes | client MAC address |
| weight | int32 | yes | airtime weight |


## update_beacon
Force beacon frame content to be updated and to start beaconing on an interface that uses start_disabled=1.

### example
`ubus call hostapd.wl5-fb update_beacon`


## wps_status
Get WPS status for BSS.

### example
`ubus call hostapd.wl5-fb wps_status`

### output
```json
{
        "pbc_status": "Disabled",
        "last_wps_result": "None"
}
```


## wps_cancel
Cancel WPS Push Button Configuration.

### example
`ubus call hostapd.wl5-fb wps_cancel`


## wps_start
Start WPS Push Button Configuration.

### example
`ubus call hostapd.wl5-fb wps_start`
