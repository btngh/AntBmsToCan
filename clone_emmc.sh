# rm clone_emmc.sh
# wget -O clone_emmc.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/clone_emmc.sh?v=$(date +%s) | bash
# chmod +x clone_emmc.sh
# ./clone_emmc.sh



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
gunzip -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz
dd if=BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img of=/dev/mmcblk1boot0 bs=1k conv=sync,notrunc
rm -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img

echo "============================================="
echo "BƯỚC 3 & 4: ĐỒNG BỘ U-BOOT VÀ CLONE HỆ ĐIỀU HÀNH..."
echo "============================================="
dd if=/dev/mmcblk0 of=/dev/mmcblk1 bs=1k skip=320 seek=320 count=1024 conv=sync,notrunc
mmc bootpart enable 1 1 /dev/mmcblk1
# Clone 7.3G dữ liệu
dd if=/dev/mmcblk0 | pv -s 7300M | dd of=/dev/mmcblk1 bs=4M conv=fsync

echo "============================================="
echo "BƯỚC 5: SỬA LỖI ĐỊNH DẠNG BLOCK BITMAP..."
echo "============================================="
#fsck.ext4 -y /dev/mmcblk1p2

echo "============================================="
echo "BƯỚC 6: CAN THIỆP HỆ THỐNG EMMC (DÙNG ECHO ĐỂ TRÁNH LỖI EOF)..."
echo "============================================="
mkdir -p /mnt/emmc_boot /mnt/emmc_rootfs
mount /dev/mmcblk1p1 /mnt/emmc_boot
mount /dev/mmcblk1p2 /mnt/emmc_rootfs

# Sửa uEnv.txt và fstab bằng lệnh echo (An toàn hơn cat <<EOL)
echo "root=/dev/mmcblk1p2 rootwait" > /mnt/emmc_boot/bananapi/bpi-r2/linux/uEnv.txt
echo -e "/dev/mmcblk1p1 /boot vfat errors=remount-ro 0 1\n/dev/mmcblk1p2 / ext4 defaults 0 0" > /mnt/emmc_rootfs/etc/fstab

# Chặn driver đồ họa và các dịch vụ gây treo máy 90s
#echo "blacklist lima" > /mnt/emmc_rootfs/etc/modprobe.d/blacklist.conf
#ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-journald.service
#ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/NetworkManager-wait-online.service
#rm -f /mnt/emmc_rootfs/lib/systemd/system/systemd-journal-flush.service
#rm -rf /mnt/emmc_rootfs/var/log/journal/*
#echo "kernel.printk = 3 4 1 3" >> /mnt/emmc_rootfs/etc/sysctl.conf

echo "============================================="
echo "BƯỚC 7: HOÀN TẤT VÀ TỰ HỦY SCRIPT TRÊN THẺ SD..."
echo "============================================="
umount /mnt/emmc_boot /mnt/emmc_rootfs
sync
# Lệnh tự xóa chính nó trên thẻ SD
rm -f "$0"
echo "XONG! Rút thẻ SD và máy sẽ tự tắt sau 5s."
sleep 5
poweroff
