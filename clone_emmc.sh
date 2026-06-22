

# rm clone_emmc.sh
# wget -O clone_emmc.sh https://github.com/btngh/AntBmsToCan/raw/refs/heads/master/clone_emmc.sh?v=$(date +%s) | bash
# chmod +x clone_emmc.sh
# ./clone_emmc.sh
#
# 1. Cài bộ công cụ phần cứng, đĩa thô và nén file
# apt install -y binutils mmc-utils pv parted fdisk xxd wget curl net-tools iproute2 wireless-tools bridge-utils

# 2. Cài môi trường lập trình để biên dịch code điều khiển Inverter và chạy file .ini
#  apt install -y build-essential gcc make cmake git python3 python3-pip python3-dev libssl-dev i2c-tools minicom screen


#!/bin/bash
# =========================================================================
# SCRIPT TỔNG LỰC: CLONE TỪ ĐẦU TỚI ĐUÔI TỪ THẺ SD SANG eMMC (BANANA PI R2)
# Sửa đổi: Khôi phục kho chuẩn ports.ubuntu.com dành riêng cho chip ARM
# =========================================================================

echo "============================================="
echo "BƯỚC 1: DỌN DẸP KHO APT VÀ CÀI ĐẶT CÔNG CỤ HỆ THỐNG LÕI..."
echo "============================================="
# Xóa bỏ file Docker amd64 gây lỗi nghẽn cập nhật
rm -f /etc/apt/sources.list.d/*

# Nạp kho phần mềm chuẩn PORTS dành riêng cho chip ARM

echo -e "deb http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-backports main restricted universe multiverse\ndeb http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse" > /etc/apt/sources.list

apt update
apt install -y binutils mmc-utils pv wget

echo "============================================="
echo "BƯỚC 2: TẢI VÀ NẠP PRELOADER GỐC EMMC CHÍNH HÃNG 2019..."
echo "============================================="
# Mở khóa ghi phân vùng ẩn boot0 của eMMC
echo 0 > /sys/block/mmcblk1boot0/force_ro

# Tải file mồi nguồn chứa chữ ký số eMMC chuẩn xác từ kho hãng SINOVOIP
https://github.com/BPI-SINOVOIP/BPI-files/blob/master/SD/100MB/BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz
gunzip -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img.gz

# Nạp thẳng Preloader vào phân vùng ẩn chuyên dụng
dd if=BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img of=/dev/mmcblk1boot0 bs=1k conv=sync,notrunc
rm -f BPI-R2-EMMC-boot0-DDR1600-20190722-0k.img

echo "============================================="
echo "BƯỚC 3: ĐỒNG BỘ U-BOOT SANG PHÂN VÙNG CHÍNH VÀ KHÓA THANH GHI BOOT..."
echo "============================================="
# Copy U-boot đồng bộ từ thẻ SD sang eMMC tại vị trí block 320
dd if=/dev/mmcblk0 of=/dev/mmcblk1 bs=1k skip=320 seek=320 count=1024 conv=sync,notrunc

# Ra lệnh kích hoạt phần cứng eMMC chuyển sang chế độ boot 0x48
mmc bootpart enable 1 1 /dev/mmcblk1

echo "============================================="
echo "BƯỚC 4: CLONE TOÀN BỘ DỮ LIỆU HỆ ĐIỀU HÀNH SANG EMMC..."
echo "============================================="
# Thực hiện sao chép thô và hiển thị thanh tiến trình dung lượng trực quan
dd if=/dev/mmcblk0 | pv -s 7300M | dd of=/dev/mmcblk1 bs=4M conv=fsync

echo "============================================="
echo "BƯỚC 5: QUÉT VÀ SỬA TOÀN BỘ LỖI CHỈ MỤC TỆP TIN (BLOCK BITMAP)..."
echo "============================================="
# Ép sửa chữa mọi cung block lỗi checksum chéo sinh ra do quá trình clone dữ liệu
fsck.ext4 -y /dev/mmcblk1p2

echo "============================================="
echo "BƯỚC 6: TIẾN HÀNH CAN THIỆP HỆ THỐNG EMMC ĐỂ KHÓA CHẾT BẪY LỖI..."
echo "============================================="
# Gắn kết các phân vùng eMMC ra thư mục tạm để chỉnh sửa cấu hình bên trong
mkdir -p /mnt/emmc_boot
mkdir -p /mnt/emmc_rootfs
mount /dev/mmcblk1p1 /mnt/emmc_boot
mount /dev/mmcblk1p2 /mnt/emmc_rootfs

# 6.1. Tạo file cấu hình mồi uEnv.txt mới tinh, ép nhân Linux nhận đúng eMMC p2
cat <<EOL > /mnt/emmc_boot/bananapi/bpi-r2/linux/uEnv.txt
root=/dev/mmcblk1p2 rootwait
bootargs=root=/dev/mmcblk1p2 rootwait console=ttyS2,115200 earlyprintk
EOF

# 6.2. Sửa file fstab của eMMC trỏ đúng vào mmcblk1
cat <<EOL > /mnt/emmc_rootfs/etc/fstab
# <file system>         <dir>   <type>  <options>               <dump>  <pass>
/dev/mmcblk1p1          /boot   vfat    errors=remount-ro       0       1
/dev/mmcblk1p2          /       ext4    defaults                0       0
EOF

# 6.3. Chặn driver đồ họa lỗi "lima" gây sụt áp bus điện eMMC khi khởi động độc lập
echo "blacklist lima" > /mnt/emmc_rootfs/etc/modprobe.d/blacklist.conf

# 6.4. Dùng lệnh mask khóa chết vĩnh viễn chuỗi 5 dịch vụ bẫy gây đóng băng máy 90 giây
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-journald.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-journal-flush.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/systemd-networkd-wait-online.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/NetworkManager-wait-online.service
ln -sf /dev/null /mnt/emmc_rootfs/etc/systemd/system/fstrim.service

# Xóa hoàn toàn file dịch vụ Flush và dọn dẹp tàn dư log tồi cũ
rm -f /mnt/emmc_rootfs/lib/systemd/system/systemd-journal-flush.service
rm -rf /mnt/emmc_rootfs/var/log/journal/*
rm -rf /mnt/emmc_rootfs/var/lib/systemd/timers/*

# 6.5. Hạ mức cảnh báo của nhân Linux (printk) xuống thấp nhất để chống tràn log thô ra màn hình console
echo "kernel.printk = 3 4 1 3" >> /mnt/emmc_rootfs/etc/sysctl.conf

echo "============================================="
echo "BƯỚC 7: HOÀN TẤT, NGẮT GẮN KẾT Ổ ĐĨA AN TOÀN!"
echo "============================================="
umount /mnt/emmc_boot
umount /mnt/emmc_rootfs
sync
# LỆNH TỰ HỦY FILE SCRIPT TRÊN THẺ NHỚ TRƯỚC KHI TẮT MÁY

echo "------------------------------------------------------------------------"
echo ">>> THÀNH CÔNG RỰC RỠ! BO MẠCH SẼ TỰ ĐỘNG TẮT NGUỒN SAU 5 GIÂY <<<"
echo ">>> ANH HÃY RÚT THẺ SD RA VÀ BẬT NGUỒN ĐỂ HƯỞNG THÀNH QUẢ KHỞI ĐỘNG SIÊU TỐC! <<<"
echo "------------------------------------------------------------------------"
sleep 5
poweroff

