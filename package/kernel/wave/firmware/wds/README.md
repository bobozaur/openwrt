# WRX600 A0 station WDS and receive BlockAck corrections

This layer pairs the station control-port transport with one fixed NETGEAR RAX40 V1 1.0.6.106 AP-firmware correction. The ordinary `wave600-firmware` package supplies the pinned original firmware bytes in the image. No full radio image is included here. The instruction overlay is applied by the existing native firmware writer while the CPUs remain in reset.

The typed UMAC loader accepts only original image size 0x180000 and SHA256 6801144d7d45d66ffdb867624d4906a6f215243cd00fa9d1611333ddf6477908, one aligned native IRAM region and swap-disabled upload. Ten aligned spans totaling 716 bytes reconstruct corrected SHA256 `f4c8d064aaf4e175d8c12f13c013277a69ea8eed446e312e77a7840052e51de6`, pinned in the generator. Other CPU images retain their ordinary loader path.

The native logical station role remains unchanged. The corrected four-address HC format retains the station association/scheduler state. Station EAPOL uses the canonical mac80211 control-port skb through the existing BSS buffer/completion owner, normalized only in that copied buffer to non-QoS three-address DATA with a 24-byte header. Explicit raw/encrypt metadata is paired with the native classifier. Native key/default-index owners project the current CCMP context; no new PN, key or sequence owner is introduced. Legacy encrypted ciphers retain the existing Ethernet transport.

The receive BlockAck change is confined to the native recipient setup owner at `0x4bb54`. It seeds the existing active one-entry window before enlarging it: an equal current/requested sequence is retained; otherwise the requested starting sequence's 12-bit predecessor is used. The unchanged native writer handles wrapping from zero to `0xfff`. This follows the matching reference transition without changing the generic sequence writer or BAR handling. Inactive windows and other active window sizes retain their original command order and arguments, including the latter's zero-sequence case. Native command flags, halt/completion waits, software sequence shadow and final release are unchanged. There is no new persistent state or per-packet operation.

## Maintainer reproduction

Provide the pinned original `ap_ram_gen6_wrx_600_real_phy.bin` separately and use GNU ARC 2.42 tools supporting little-endian ARCv2 HS. Set `original_ap`, `wds_tools`, `wds_build`, `arc_bin`, `arc_lib` and `loader_header` to local paths. `wds_tools` is this directory; `loader_header` is the prepared source `wireless/driver/shared/wave_wds_ap.h`.

```sh
mkdir -p "$wds_build"
LD_LIBRARY_PATH="$arc_lib" "$arc_bin/arc-linux-gnu-as" -EL -mcpu=archs --fatal-warnings \
  -I "$(dirname "$original_ap")" -o "$wds_build/rax40-wds.o" "$wds_tools/rax40-wds.S"
LD_LIBRARY_PATH="$arc_lib" "$arc_bin/arc-linux-gnu-ld.bfd" -m arcv2elf --no-relax \
  -T "$wds_tools/rax40-wds.ld" -o "$wds_build/rax40-wds.elf" "$wds_build/rax40-wds.o"
for section in ba_owner default_owner key_commit_hook keyslot_owner hif lmac1 sta1_hook lmac2 sta2_hook; do
  LD_LIBRARY_PATH="$arc_lib" "$arc_bin/arc-linux-gnu-objcopy" \
    --dump-section ".$section=$wds_build/$section.bin" "$wds_build/rax40-wds.elf"
done
python3 "$wds_tools/generate-loader-data.py" --original "$original_ap" \
  --sections "$wds_build" --output "$loader_header"
```

The generator verifies the exact original, section sizes, complete reconstructed SHA256 and the native ten-span reconstruction before producing the header. Original firmware acquisition/use rights remain separate from these source tools; public availability alone is not a redistribution grant. This focused branch remains outside an OpenWrt submission until applicable modification and redistribution permissions are resolved. `LicenseRef-NETGEAR-RAX40-Firmware` records that boundary and grants no rights. The kernel base supplies the direct SHA256 library, and the driver/RF build must use matching coherent headers.

## Validation limits

The matched runtime passed ordinary association, multiple PTK epochs/rekeys/reconnect, foreign-source forwarding and standard CCMP authentication. A same-key sequence of ordinary DATA, raw KeyRequest and raw M2 used increasing shared PNs without conflicts. These results establish the focused mechanism, not every cipher or optional feature. PMF/SAE/OWE runtime qualification and final integrated durability/throughput acceptance are separate.

The corrected BlockAck transition passed matched runtime tests in both native AP and station roles. Protected ADDBA accepted starting sequence 1; the active 64-entry receive window retained that sequence before DATA. The first MPDU had valid CCMP authentication and reached the native IP interface, followed by the remaining burst. Protected SA Query and bidirectional traffic after reconnect also passed. These bounded results do not establish every sequence/window combination or undocumented hardware semantics.

For the first-MPDU qualification, keep the same air and Ethernet captures active from association through the last receive-state read. After the existing ARP-only neighbor probe, admit the DATA burst only when the native `rxpp_ram` control-word read shows an active negotiated window of 64; a one-entry window is not a completed-setup witness. Record the full word and its read timing before DATA, then read it again immediately after the finite burst before slower statistics queries. Correlate every intervening peer TID0 frame and the accepted ADDBA starting sequence, rather than assigning universal sequence semantics from one isolated RAM value.
