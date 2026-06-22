#
# wget https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/clone_emmc.sh
# chmod +x clone_emmc.sh
# ./clone_emmc.sh

#!/bin/bash
# =========================================================================
# SCRIPT TỰ ĐỘNG CHUYỂN UBUNTU NOBLE TỪ SD SANG eMMC CHO BANANA PI R2
# Thực hiện bởi: Anh Nam & AI Assistant
# =========================================================================

echo "============================================="
echo "BƯỚC 1: DỌN DẸP KHO APT VÀ CÀI ĐẶT CÔNG CỤ LÕI..."
echo "============================================="
rm -f /etc/apt/sources.list.d/*
cat <<EOF > /etc/apt/sources.list
deb http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports noble-backports main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse
EOF

apt update && apt install -y binutils mmc-utils pv wget

echo "============================================="
echo "BƯỚC 2: TẢI VÀ NẠP PRELOADER GỐC EMMC 2019..."
echo "============================================="
echo 0 > /sys/block/mmcblk1boot0/force_ro
wget -O preloader_emmc.img.gz https://github.com
gunzip -f preloader_emmc.img.gz
dd if=preloader_emmc.img of=/dev/mmcblk1boot0 bs=1k conv=sync,notrunc
rm -f preloader_emmc.img

echo "============================================="
echo "BƯỚC 3: ĐỒNG BỘ U-BOOT SANG BỘ NHỚ EMMC CHÍNH..."
echo "============================================="
dd if=/dev/mmcblk0 of=/dev/mmcblk1 bs=1k skip=320 seek=320 count=1024 conv=sync,notrunc
mmc bootpart enable 1 1 /dev/mmcblk1

echo "============================================="
echo "BƯỚC 4: CLONE TOÀN BỘ HỆ ĐIỀU HÀNH SANG EMMC (MẤT VÀI PHÚT)..."
echo "============================================="
dd if=/dev/mmcblk0 | pv -s 7300M | dd of=/dev/mmcblk1 bs=4M conv=fsync

echo "============================================="
echo "BƯỚC 5: SỬA LỖI ĐỊNH DẠNG BLOCK VÀ CHỈ MỤC TỆP TIN..."
echo "============================================="
fsck.ext4 -y /dev/mmcblk1p2

echo "============================================="
echo "BƯỚC 6: CAN THIỆP HỆ THỐNG EMMC ĐỂ CHẶN TREO VÀ TRÀN LOG..."
echo "============================================="
mkdir -p /mnt/emmc_boot
mkdir -p /mnt/emmc_rootfs
mount /dev/mmcblk1p1 /mnt/emmc_boot
mount /dev/mmcblk1p2 /mnt/emmc_rootfs

# Ép tham số mồi nhận diện đúng eMMC
cat <<EOF > /mnt/emmc_boot/bananapi/bpi-r2/linux/uEnv.txt
root=/dev/mmcblk1p2 rootwait
bootargs=root=/dev/mmcblk1p2 rootwait console=ttyS2,115200 earlyprintk
EOF

# Sửa file fstab trỏ đúng về mmcblk1
cat <<EOF > /mnt/emmc_rootfs/etc/fstab
# <file system>         <dir>   <type>  <options>               <dump>  <pass>
/dev/mmcblk1p1          /boot   vfat    errors=remount-ro       0       1
/dev/mmcblk1p2          /       ext4    defaults                0       0
EOF

# Chặn driver đồ họa gây sụt áp eMMC
echo "blacklist lima" > /mnt/emmc_rootfs/etc/modprobe.d/blacklist.conf

# Khóa chết các dịch vụ bẫy gây treo máy 90 giây
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-journald.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-networkd-wait-online.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/NetworkManager-wait-online.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/fstrim.service
rm -f /mnt/emmc_rootfs/lib/systemd/system/systemd-journal-flush.service
rm -rf /mnt/emmc_rootfs/var/log/journal/*
rm -rf /mnt/emmc_rootfs/var/lib/systemd/timers/*

# Chặn vĩnh viễn các dòng log tràn màn hình
echo "kernel.printk = 3 4 1 3" >> /mnt/emmc_rootfs/etc/sysctl.conf

echo "============================================="
echo "BƯỚC 7: HOÀN TẤT, NGẮT Ổ ĐĨA AN TOÀN!"
echo "============================================="
umount /mnt/emmc_boot
umount /mnt/emmc_rootfs
sync

echo "--------------------------------------------------------"
echo ">>> THÀNH CÔNG! HỆ THỐNG SẼ TỰ TẮT NGUỒN SAU 5 GIÂY <<<"
echo ">>> ANH HÃY RÚT THẺ SD RA VÀ BẬT NGUỒN ĐỂ HƯỞNG THÀNH QUẢ! <<<"
echo "--------------------------------------------------------"
sleep 5
poweroff
