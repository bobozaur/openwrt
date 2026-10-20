# WAVE600 firmware experiments

Historical patcher and ARC assembly for the exact WAVE600 06.00.08.00.87
firmware cohort identified in fix-firmware.py. The sources cover protected
RX copying, BSS colour, stream setup, fixed DATA power, RTS and TXOP handling.

This directory has no OpenWrt build or install integration. The production
driver uses unchanged vendor firmware. These experiments require their
matching host-side interfaces and independent qualification before use;
restoring this directory alone does not enable a supported configuration.

No vendor firmware is included. Source licensing does not establish rights
to distribute the original or modified radio binaries. Preserve the pinned
input and output identities when investigating this historical work.
