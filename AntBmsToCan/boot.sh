# apt install wget
# rm clone_emmc.sh
# wget -O clone_emmc.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/boot.sh?v=$(date +%s)
# chmod +x boot.sh
# ./boot.sh



#!/bin/bash
# =========================================================================
# SCRIPT TỔNG LỰC: CLONE SD -> EMMC (BANANA PI R2) - BẢN FIX DỨT ĐIỂM
# Tác giả: Anh Nam & AI Assistant
# =========================================================================

echo "============================================="
echo "BƯỚC 1: CẤU HÌNH KHO PORTS VÀ BÙ ĐẮP LỆNH BỊ LƯỢC BỎ..."
echo "============================================="
rm -f /etc/apt/sources.list.d/*
# Ép cấu hình kho ports.ubuntu.com chuẩn xác
# echo -e "deb http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-backports main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse" > /etc/apt/sources.list

apt update
# Cài bù các lệnh bị lược bỏ trong bản Noble Minimal
apt install -y binutils mmc-utils pv wget parted fdisk xxd net-tools build-essential gcc make cmake git python3 python3-pip

echo "============================================="
echo "BƯỚC 2: NẠP PRELOADER GỐC EMMC 2019 (FIX LỖI LỆNH TẢI)..."
echo "============================================="
echo 0 > /sys/block/mmcblk1boot0/force_ro
# Đã thêm lệnh wget và dùng link Raw chuẩn
wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz
wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/u-boot-2019.07-bpi-r2-2k.img.gz
gunzip -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz
dd if=BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img of=/dev/mmcblk1boot0 bs=1k conv=sync,notrunc
gunzip -c u-boot-2019.07-bpi-r2-2k.img.gz | dd of=/dev/mmcblk1boot0 bs=1024 seek=2 status=progress
rm -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img


echo "XONG! Rút thẻ SD và máy sẽ tự tắt sau 5s."
sleep 5
poweroff
