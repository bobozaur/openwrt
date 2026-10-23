# SPDX-License-Identifier: GPL-2.0-only

define Profile/AX50_FULL
  NAME:=TP-Link Archer AX50 v1 (full services)
  PACKAGES:= \
	6in4 6rd 6to4 apk-mbedtls \
	ax50-caldata ax50-leds base-files block-mount \
	conntrack dnsmasq-full dosfstools dropbear \
	ds-lite e2fsprogs ethtool exfat-fsck \
	exfat-mkfs hfsfsck hostapd-utils input-support ip-bridge \
	ip6tables-nft iptables-nft kmod-bonding kmod-fs-exfat \
	kmod-fs-ext4 kmod-fs-hfsplus kmod-fs-ntfs3 kmod-fs-vfat \
	kmod-gpio-button-hotplug kmod-grx500-eth kmod-leds-gpio kmod-ledtrig-network \
	kmod-loop kmod-nf-nathelper-pptp kmod-nft-xfrm kmod-nlmon \
	kmod-usb-dwc3-grx500 kmod-usb-ledtrig-usbport kmod-usb-net-cdc-ncm kmod-usb-net-rndis \
	kmod-usb-storage libevdev libjpeg-turbo libncurses \
	libudev-zero logd luci luci-app-acl \
	luci-app-adblock luci-app-banip luci-app-ddns luci-app-minidlna \
	luci-app-mwan3 luci-app-nlbwmon luci-app-omcproxy luci-app-pbr \
	luci-app-samba4 luci-app-sqm luci-app-strongswan-swanctl luci-app-uhttpd \
	luci-app-upnp luci-app-usteer luci-app-wifischedule luci-app-xl2tpd \
	luci-proto-openvpn luci-proto-wireguard luci-proto-xfrm luci-ssl \
	miniupnpd-nftables mtd odhcp6c odhcpd-ipv6only \
	openvpn-mbedtls ppp-mod-pppoe ppp-mod-pptp pptpd \
	procd-ujail strongswan strongswan-charon strongswan-mod-constraints strongswan-mod-kernel-netlink \
	strongswan-mod-openssl strongswan-mod-pem strongswan-mod-pkcs1 strongswan-mod-pubkey \
	strongswan-mod-random strongswan-mod-revocation strongswan-mod-socket-default strongswan-mod-updown \
	strongswan-mod-x509 tc-tiny tcpdump uboot-envtools \
	uci urandom-seed urngd video-support vsftpd-tls \
	wave-scripts wpad-mbedtls wsdd2
endef

define Profile/AX50_FULL/Description
 Full AX50 image with LuCI, native wireless, VPN and multi-WAN routing,
 multicast, traffic management, USB storage and file/media services.
 Service selection supplies no installation-specific network configuration.
endef

$(eval $(call Profile,AX50_FULL))
