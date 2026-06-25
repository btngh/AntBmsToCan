# rm clone_emmc.sh
# wget boot.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/boot.sh?v=$(date +%s) | bash
# chmod +x boot.sh
# ./boot.sh



#!/bin/bash
# =========================================================================
# SCRIPT TỔNG LỰC: CLONE SD -> EMMC (BANANA PI R2) - BẢN FIX DỨT ĐIỂM
# Tác giả: Anh Nam & AI Assistant
# =========================================================================


echo "NẠP PRELOADER GỐC EMMC 2019 (FIX LỖI LỆNH TẢI)..."
echo "============================================="
echo 0 > /sys/block/mmcblk1boot0/force_ro
# Đã thêm lệnh wget và dùng link Raw chuẩn
wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-preloader-DDR1600-20191024-2k.img.gz
dd if=/dev/zero of=/dev/mmcblk1boot0 bs=1k count=1024 conv=notrunc
gunzip -c BPI-R2-preloader-DDR1600-20191024-2k.img.gz  | dd of=/dev/mmcblk1boot0 bs=1024 seek=0
echo 1 > /sys/block/mmcblk1boot0/force_ro



wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/u-boot-2019.07-bpi-r2-2k.img.gz
gunzip -c /u-boot-2019.07-bpi-r2-2k.img.gz | dd of=/dev/mmcblk1 bs=1k seek=2 count=1022
sudo mmc bootpart enable 1 1 /dev/mmcblk1

sync
# Lệnh tự xóa chính nó trên thẻ SD
rm -f "$0"
echo "XONG! Rút thẻ SD và máy sẽ tự tắt sau 5s."
sleep 5
poweroff
