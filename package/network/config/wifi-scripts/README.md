# Wireless configuration and process ownership

The shell and ucode backends map the `wifi-iface` option `wmm` to hostapd's
`wmm_enabled`, defaulting to enabled. Disabling it permits legacy operation;
hostapd's normal checks still govern modes that require WMM.

The ucode AP generator maps explicit `isolate` to hostapd's `ap_isolate`.
Multicast-to-unicast or proxy ARP additionally requests driver isolation when
the interface belongs to a configured bridge, so the bridge controls delivery.
On an unbridged AP those options do not disable native peer forwarding. Both
enabled and disabled values are emitted when the configuration is replaced.

Each radio registers a hostapd or supplicant process dependency only when that
process owns one of its configured interfaces. Empty configuration requests still
retire interfaces from a previous role, but do not attach an AP-only radio to the
shared supplicant process or a STA-only radio to shared hostapd. A process exit
continues to restart every radio that actually depends on it.

When a configured mac80211 PHY returns after removal, the existing wireless
retry request also reconciles a radio whose setup state remained up. If its
recorded PHY disappeared and the normal configuration lookup finds a live
replacement, netifd runs the old radio handler's teardown before setting up the
replacement. This retires stale hostapd and supplicant configuration before
reusing interface names. Radios with a live PHY, administratively stopped or
disabled radios, and handlers already setting up or tearing down keep their
existing lifecycle.
