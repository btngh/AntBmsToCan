
# rm clone_emmc.sh
# wget qq.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/qq.sh?v=$(date +%s)
# chmod +x qq.sh
# ./qq.sh

#!/bin/bash
# =========================================================================
# SCRIPT TỔNG LỰC: CLONE SD 64GB -> EMMC 32GB (FRANK-W MANUAL COPY METHOD)
# PHẦN 1/2 - ĐỊNH DẠNG PHẦN CỨNG & VÁ BOOTLOADER CHUẨN XÁC
# =========================================================================

set -e

echo "============================================="
echo " BƯỚC 1: ĐỒNG BỘ GIỜ GMT+7 VÀ CÀI ĐẶT CÔNG CỤ..."
echo "============================================="
# 1. Ép cứng đồng hồ hệ thống về thời gian thực tế năm 2026 để thông mạch bảo mật SSL
date -s "2026-06-25 11:30:00"

# 2. Định hình múi giờ chuẩn Việt Nam (GMT+7)
timedatectl set-timezone Asia/Ho_Chi_Minh || true

# 3. Kích hoạt lại dịch vụ NTP sau khi đã mồi giờ thủ công thành công
timedatectl set-ntp true || true
systemctl restart systemd-timesyncd || true

# 4. Tiến hành cập nhật kho ứng dụng hệ thống không sợ lỗi SSL chặn chứng chỉ
apt-get update
apt-get install -y binutils mmc-utils pv parted fdisk xxd dosfstools rsync

SRC="/dev/mmcblk0"
DST="/dev/mmcblk1"
BOOT0="/dev/mmcblk1boot0"

if [ ! -b "$DST" ]; then
    echo "ERROR: Không tìm thấy chip eMMC mới ($DST)!"
    exit 1
fi

echo "Gỡ bỏ các phân vùng eMMC đang bị chiếm dụng..."
umount ${DST}p* 2>/dev/null || true
echo "NẠP boot0 GỐC EMMC 2019 (FIX LỖI LỆNH TẢI)..."
echo "============================================="
echo 0 > /sys/block/mmcblk1boot0/force_ro
# Đã thêm lệnh wget và dùng link Raw chuẩn
wget -O boot0.img.gz https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/BPI-R2-EMMC-boot0-DDR1600-20191024-0k.img.gz?v=$(date +%s)
dd if=/dev/zero of=/dev/mmcblk1boot0 bs=1k count=1024 conv=notrunc
gunzip -c boot0.img.gz  | dd of=/dev/mmcblk1boot0 bs=1024 seek=0
echo 1 > /sys/block/mmcblk1boot0/force_ro



wget -O uboot.img.gz https://github.com/BPI-SINOVOIP/BPI-files/raw/refs/heads/master/SD/100MB/u-boot-2019.07-bpi-r2-2k.img.gz?v=$(date +%s)
gunzip -c uboot.img.gz | dd of=/dev/mmcblk1 bs=512 seek=640 conv=notrunc
sudo mmc bootpart enable 1 1 /dev/mmcblk1

sync







echo "============================================="
echo " BƯỚC 2: SAO CHÉP CẤU TRÚC BẢNG PHÂN VÙNG (SFDISK)..."
echo "============================================="
# Trích xuất bảng phân vùng từ SD và ép cấu trúc sang eMMC mới
sfdisk -d $SRC > /tmp/parttable.dat
sfdisk $DST < /tmp/parttable.dat
partprobe $DST

echo "============================================="
echo " BƯỚC 3: ĐỊNH DẠNG HỆ THỐNG TỆP TIN SẠCH (MKFS)..."
echo "============================================="
# Tạo định dạng đĩa BPI-BOOT (FAT32) và BPI-ROOT (EXT4) vừa vặn cấu trúc eMMC 32GB
mkfs.vfat -F 32 -n BPI-BOOT ${DST}p1
mkfs.ext4 -F -L BPI-ROOT ${DST}p2

echo "============================================="
echo " BƯỚC 4: VÁ U-BOOT VÀO VÁCH CHUẨN FRANK-W..."
echo "============================================="
# Nạp U-Boot từ file ảnh gốc (hoặc từ thẻ SD) vào đúng vách seek=2 theo tài liệu
#dd if=$SRC of=$DST bs=1k seek=2 count=1022 conv=notrunc
#sync

echo "============================================="
echo " BƯỚC 5: GẮN KẾT ĐĨA MỒI ĐỂ CHUẨN BỊ SAO CHÉP FILE..."
echo "============================================="
mkdir -p /mnt/emmc/boot
mkdir -p /mnt/emmc/root

mount ${DST}p2 /mnt/emmc/root
mount ${DST}p1 /mnt/emmc/boot
echo "============================================="
echo " BƯỚC 6: CẤU HÌNH ĐƯỜNG DẪN TRONG FILE FSTAB..."
echo "============================================="
# Sao lưu file fstab hiện tại của hệ thống
cp /etc/fstab /etc/fstab.bak

# Thêm cấu hình mount-point tạm thời vào fstab nếu chưa có
grep -q "/mnt/emmc/root" /etc/fstab || echo "/dev/mmcblk1p2          /mnt/emmc/root  ext4    errors=remount-ro,noauto  0       1" >> /etc/fstab
grep -q "/mnt/emmc/boot" /etc/fstab || echo "/dev/mmcblk1p1          /mnt/emmc/boot  vfat    defaults,noauto           0       0" >> /etc/fstab

echo "============================================="
echo " BƯỚC 7: ĐỒNG BỘ TOÀN BỘ HỆ ĐIỀU HÀNH (RSYNC)..."
echo "============================================="
# Chạy rsync nhặt từng file thực tế, loại trừ các thư mục ảo để chống tràn đĩa 32GB
rsync -aAXv --exclude={"/dev/*","/proc/*","/sys/*","/tmp/*","/run/*","/mnt/*","/media/*","/lost+found","/boot/*"} / /mnt/emmc/root/

echo "============================================="
echo " BƯỚC 8: SAO CHÉP KERNEL VÀ MODULES VẬT LÝ..."
echo "============================================="
# Tạo cấu trúc thư mục lưu trữ Kernel chuẩn Banana Pi
mkdir -p /mnt/emmc/boot/bananapi/bpi-r2/linux
if [ -f /boot/bananapi/bpi-r2/linux/uImage ]; then
    cp /boot/bananapi/bpi-r2/linux/uImage /mnt/emmc/boot/bananapi/bpi-r2/linux/
fi

# Sao chép các gói Driver/Modules đi kèm với phiên bản Kernel hiện tại
mkdir -p /mnt/emmc/root/lib/modules/
cp -r /lib/modules/$(uname -r) /mnt/emmc/root/lib/modules/

echo "============================================="
echo " BƯỚC 9: ĐỔI ĐỊNH HƯỚNG BOOT TỪ SD SANG EMMC..."
echo "============================================="
# Thay thế chuỗi mmcblk0 (SD) thành mmcblk1 (eMMC) bên trong file uEnv.txt mới
if [ -f /boot/bananapi/bpi-r2/linux/uEnv.txt ]; then
    sed 's/mmcblk0/mmcblk1/g' /boot/bananapi/bpi-r2/linux/uEnv.txt > /mnt/emmc/boot/bananapi/bpi-r2/linux/uEnv.txt
else
    # Khởi tạo file uEnv.txt cứu hộ nếu file gốc không tồn tại
    cat <<EOF > /mnt/emmc/boot/bananapi/bpi-r2/linux/uEnv.txt
bpi=banana
board=bpi-r2
kernel=bananapi/bpi-r2/linux/uImage
root=/dev/mmcblk1p2 rootfstype=ext4 rootwait
bootopts=clearcpuid=0
EOF
fi

# Trả lại file fstab sạch cho eMMC để khi khởi động nó tự mount chính nó
if [ -f /mnt/emmc/root/etc/fstab ]; then
    sed -i 's/mmcblk0p1/mmcblk1p1/g' /mnt/emmc/root/etc/fstab
    sed -i 's/mmcblk0p2/mmcblk1p2/g' /mnt/emmc/root/etc/fstab
fi

echo "============================================="
echo " BƯỚC 10: GIẢI PHÓNG BỘ NHỚ ĐỆM VÀ HOÀN TẤT..."
echo "============================================="
sync
umount /mnt/emmc/boot || true
umount /mnt/emmc/root || true

# Khôi phục lại file fstab gốc cho thẻ SD mồi
mv /etc/fstab.bak /etc/fstab

echo "========================================================"
echo " CLONE MANUAL THÀNH CÔNG! MÁY TỰ TẮT NGUỒN SAU 5 GIÂY..."
echo " Hãy rút thẻ SD ra ngoài và bật nguồn lại để tận hưởng eMMC mới."
echo "========================================================"
sleep 5
poweroff
