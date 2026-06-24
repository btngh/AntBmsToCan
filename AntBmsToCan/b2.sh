# apt install wget
# rm clone_emmc.sh
# wget -O boot.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/AntBmsToCan/b2.sh?v=$(date +%s)
# chmod +x b2.sh
# ./boot.sh

#!/bin/bash
# =====================================================================
# SCRIPT CLONE SYSTEM TO NEW EMMC - BANANA PI BPI-R2 (FRANK-W SPEC)
# =====================================================================
set -e

echo "[1/5] Installing hardware partition utilities..."
apt-get update && apt-get install -y binutils mmc-utils pv parted fdisk xxd

SRC="/dev/mmcblk0"
DST="/dev/mmcblk1"
BOOT0="/dev/mmcblk1boot0"

if [ ! -b "$DST" ]; then
    echo "ERROR: New eMMC ($DST) not found!"
    exit 1
fi

echo "[2/5] Cleaning and unmounting destination drive..."
umount ${DST}p* 2>/dev/null || true

echo "[3/5] Flashing Preloader to eMMC boot0 partition..."
# Kích hoạt quyền ghi vào phân vùng ẩn boot0
echo 0 > /sys/block/mmcblk1boot0/force_ro

# Xóa trắng 2MB đầu phân vùng boot0 để dọn sạch lỗi kẹt
dd if=/dev/zero of=$BOOT0 bs=1M count=2 conv=notrunc

# Tải và nạp bản Preloader thuần eMMC (Điểm bắt đầu 0k-offset) theo tài liệu Frank-W
wget --no-check-certificate -O /tmp/preloader_emmc.img https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-preloader-DDR1600-20190722-2k.img.gz
gunzip -c /tmp/preloader_emmc.img.gz | dd of=$BOOT0 bs=1k seek=0 conv=notrunc

echo "[4/5] Cloning core data and operating system..."
# Sao chép bảng phân vùng MBR sạch từ thẻ SD sang eMMC
dd if=$SRC of=$DST bs=512 count=1 conv=notrunc
partprobe $DST

# Trích xuất u-boot.bin đang chạy trên SD và nạp chuẩn vào vách 320k-offset của eMMC
dd if=$SRC of=/tmp/u-boot-current.bin bs=1k skip=320 count=1024
dd if=/tmp/u-boot-current.bin of=$DST bs=1k seek=320 conv=notrunc

# Sao chép dữ liệu phân vùng hệ thống (Giới hạn đệm 128K chống nghẽn I/O và tràn RAM)
SIZE=$(blockdev --getsize64 $SRC)
dd if=$SRC bs=128K conv=noerror,fdatasync | pv -s $SIZE | dd of=$DST bs=128K conv=noerror,fdatasync

echo "[5/5] Configuring eMMC Partition Config Register..."
sync
# Ép thanh ghi phần cứng chip eMMC chuyển trạng thái sang cấu hình khởi động 0x48
mmc bootpart enable 1 1 $DST
echo 1 > /sys/block/mmcblk1boot0/force_ro
partprobe $DST

echo "========================================================"
echo " CLONE SUCCESSFUL! Power off, remove SD and boot from eMMC."
echo "========================================================"
