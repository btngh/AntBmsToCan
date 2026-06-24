# apt install wget
# rm clone_emmc.sh
# wget -O b2.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/AntBmsToCan/b2.sh?v=$(date +%s)
# chmod +x b2.sh
# ./boot.sh

#!/bin/bash
set -e
echo "Starting clone process..."
# 1. Cài đặt công cụ
apt-get update && apt-get install -y rsync parted dosfstools mmc-utils

# 2. Xóa và phân vùng lại eMMC (32GB)
DST="/dev/mmcblk1"
parted -s $DST mklabel msdos
parted -s $DST unit MiB mkpart primary fat32 -- 100MiB 356MiB
parted -s $DST unit MiB mkpart primary ext4 -- 356MiB 100%

# 3. Nạp bootloader (Sử dụng lệnh dd ghi vào boot partition)
dd if=/dev/zero of=/dev/mmcblk1boot0 bs=1M count=2
# Tải và nạp bootloader qua wget...

# 4. Format và Mount
mkfs.vfat -F 32 -n BPI-BOOT ${DST}p1
mkfs.ext4 -F -L BPI-ROOT ${DST}p2
mkdir -p /mnt/src /mnt/dst
mount /dev/mmcblk0p2 /mnt/src # Tạm thời giả định nguồn
mount ${DST}p2 /mnt/dst

# 5. Rsync dữ liệu (Đảm bảo dung lượng 32GB)
rsync -aAXv --exclude={"/dev/*","/proc/*","/sys/*","/tmp/*","/run/*"} /mnt/src/ /mnt/dst/

# 6. Sửa cấu hình boot, unmount, và hoàn tất
umount /mnt/dst
echo "Clone successful!"
