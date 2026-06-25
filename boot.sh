# rm clone_emmc.sh
# wget -O boot.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/boot.sh?v=$(date +%s) | bash
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
# wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz 
wget https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-EMMC-boot0-DDR1600-0k-0905.img.gz
# gunzip -c BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img | dd of=/dev/mmcblk1boot0 bs=1024 seek=0
gunzip -c BPI-R2-EMMC-boot0-DDR1600-0k-0905.img.gz | dd of=/dev/mmcblk1boot0 bs=1024 seek=0
sudo mmc bootpart enable 1 1 /dev/mmcblk1
echo 1 > /sys/block/mmcblk1boot0/force_ro

sync
# Lệnh tự xóa chính nó trên thẻ SD
rm -f "$0"
echo "XONG! Rút thẻ SD và máy sẽ tự tắt sau 5s."
sleep 5
poweroff
