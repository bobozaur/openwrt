# rtw88 transport ownership

The exported core interfaces and their bus/chip consumers form one module cohort.
The `kmod-rtw88` ABI version follows the backports version and package release,
using the same package mechanism as cfg80211/mac80211. Install matching consumers
when the core interface changes.

The USB transport retains each original mac80211 frame until transfer completion
or cancellation. A separately allocated USB aggregation buffer is only a transport
container; it is freed independently and never submitted as another MPDU to
mac80211. Aggregation admission includes alignment padding. Rejected endpoint or
DMA admission leaves the original frame with its caller for retirement.

USB TX and RX workers keep their existing bounded batches. A batch that reaches
its limit with work remaining queues another invocation. TX continuation uses the
same admission lock as key replacement and stop; an explicit key drain drives its
own batches while ordinary admission is closed.

Terminal USB disconnect poisons the fixed RX URBs before retiring deferred
resubmission and receive work. No RX producer or consumer may survive into
mac80211 unregistration.

USB disconnect also marks the transport as unregistering before mac80211 retires
its interfaces and keys. Accepted TX work is stopped, pending firmware recovery
is joined outside its mutex, and terminal key/flush callbacks cannot restart
hardware or reopen data admission. This terminal path does not suppress active
key-update failures or their recovery. A deauthorized USB device is not reset
by the driver's disconnect callback: USB core retains authority over whether it
may be enumerated and rebound. Authorized module unbind retains the existing
reset behavior.

The optional `rtw88_usb.force_usb2` parameter defaults to N and is evaluated only
at a fresh RTL8822B probe with efuse mode-switch permission. A USB3 probe can
request USB2 before mac80211 registration or RX submission, then retires through
the existing unwind path. Transition register transfers and their always-on
synchronization writes must complete fully. A request is not proof of a new USB2
enumeration; the new physical device must be admitted independently. Changing
the parameter does not alter an attached interface.

The shared RX descriptor parser receives the valid extent supplied by USB, SDIO
or PCI. It checks the descriptor, PHY information, shift, payload and required
frame header before consumers access them. A full-size completed USB transfer is
valid when its contained records satisfy those checks.

Successful USB transfer completion proves delivery to the device, not an on-air
ACK. Requested firmware TX reports remain available to management exchanges.
Ordinary data completions do not provide complete retry/failure totals, so USB
uses the mac80211 `NO_DATA_TX_STATUS` contract. Driver-supplied authoritative
statistics remain usable.

Requested firmware TX reports use the upstream timeout policy: PCIe waits
500 ms, while USB and SDIO wait 2500 ms before retiring unreported frames.
This backports Realtek's
[bbd4f8e9217162a79cd0b016b646b3afcbbf7629](https://lore.kernel.org/linux-wireless/20260924004637.9958-1-pkshih@realtek.com/)
using the [standard OpenWrt patch](https://github.com/openwrt/openwrt/commit/3ee349d4c6e45d4e20b0f0c6a194d952a35c6123).
It preserves reports that USB/SDIO firmware can return after the former 500 ms
deadline. It changes neither USB transfer completion nor key-drain ownership,
and does not establish RF transmission, an ACK, or recovery of a missing AP.
The QoS-Null classification fix, upstream `737e980e1298`, is already included
in backports 7.2 and is not patched again.

The package selects `rtw88_core.usb_sw_crypto=1` at module load. USB keys stay in
mac80211: key installation requests software fallback before any hardware key
table or TX-drain operation. TX descriptors request no hardware encryption, and
RX rejects frames reported as hardware-decrypted. For CCMP, mac80211 authenticates
the ciphertext before advancing replay state. Common security initialization
remains enabled; firmware WoWLAN is not advertised while keys are software-owned.
The parameter cannot change on a loaded module and does not select software
crypto for PCI or SDIO.

When the hardware-key policy is selected, the USB key-update boundary drains
accepted TX frames, submitted URBs and device TX pages before CAM changes. This
is a TX guarantee. The RX descriptor does not identify the temporal-key
generation that decrypted a queued frame, and buffer-length validation or
workqueue draining alone does not establish that generation. Safe replacement
of an uploaded PTK still requires a complete device-to-mac80211 RX generation
fence. The selected software policy avoids admitting this untagged plaintext;
it does not identify the cause of historical rekey stalls.

USB continues to withhold `NL80211_EXT_FEATURE_CAN_REPLACE_PTK0`. Its hardware-key
receive path cannot guarantee that old-key plaintext stops at the return from
`DISABLE_KEY`. Other transports keep their existing advertisement. mac80211's
ordinary warning and queue-flush path applies when replacing an uploaded PTK.

The native hostapd and supplicant default `wpa_deny_ptk0_rekey=0` still allows
PTK0 rekeys. An operator selecting `wpa_deny_ptk0_rekey=1` (allow only when the
local driver supports safe replacement) now receives the native deny/reconnect
behavior on USB. No policy value is forced, no replay check is relaxed, and the
USB TX drain remains in place for hardware-key updates.

FIFO dumps preserve the device byte order for reserved-page inspection and
firmware crash-header decoding on either host endianness. The reader checks
byte ranges against FIFO capacities and restores its original packet-buffer
selector and RX clock-gate state. Reserved-page debugfs reads and configuration
use the device mutex shared with recovery; invalid page ranges are rejected
before allocation.

The root-readable `tx_buffer` debugfs file captures the complete physical TX
FIFO once when opened and serves that owned binary copy on subsequent reads.
The snapshot requires running, awake hardware and excludes concurrent
stop/recovery through the device mutex. Sleeping hardware returns busy without
changing its power-save state. Signal interruption or terminal teardown stops the
register walk and restores the FIFO selector and clock-gate state before the
incomplete copy is discarded. Host allocation uses virtual memory, so the
complete FIFO does not require one physically contiguous allocation.

The hardware continues operating during this sequential snapshot. An exact
fresh packet-byte match can establish deposition in TX memory, but missing
bytes do not establish intake loss: ordinary packet layout, fragmentation and
retention must be known separately. The file does not decode queue heads or
assert an on-air result. Register transport errors still require the existing
bus completion evidence because the HCI register-read API returns values only.

On RTL8822B, the root-readable `tx_gate` file caches one short observation
per open: ordinary queue owners/counts, stop/sleep/drop and TX control words,
native report indices, current gain/EDCCA thresholds, and source-defined
CCA/EDCCA debug outputs. The device mutex excludes stop/recovery, concurrent
samplers and raw register writers. The sampler rejects sleeping hardware or
an occupied debug mux, changes only its selector and clock, and restores their
complete original values while verifying the unchanged header. Subsequent
reads use only the saved text and retain no device pointer.

Mux admission is restricted to normal RTL8822B firmware 30.20.0 with H2C
format 14. Debugfs eligibility uses the backports `CPTCFG_RTW88_DEBUGFS`
configuration guard. The driver tracks an uninterrupted firmware lifetime without LPS,
inactive PS, P2P, TDMA or other unsupported offloads. Power requests, recovery,
raw debug writes and uncertain completion invalidate that eligibility until
a successful fresh firmware download. This affects diagnostic admission only.

The sampler can preserve either the PHY table's inactive mux state or the CCA
state left by a completed current IQK. IQK qualification passively checks the
pre-submission completion marker and uses the existing wait and clear. A stale
marker or timeout cannot be repaired by a later IQK in the same firmware
lifetime. The original calibration sequence and power policies remain intact;
no firmware-owned selector is borrowed solely because its value looks idle.

The same snapshot retains five native RTL8822B idle-power reports and the
current gains. It briefly latches each report without pausing DIG or changing
gain, RF settings or EDCCA thresholds. The latch must match its supported
PHY-table idle state; an occupied or different state is rejected. Its
full original value is restored on every exit after mutation. Raw signed
half-dB report bytes have no ready bit or freshness counter, so identical
values cannot establish continuous interference or calibrated ambient power.

These samples are sequential and affect timing. They are neither busy-time
measurements nor per-packet transmission verdicts. Register transfer completion
and restoration still require independent bus evidence; the value-only HCI
API cannot certify them. No transmit gate, power policy, key or threshold is
changed by this observation.
