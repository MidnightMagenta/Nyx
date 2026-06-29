#! /bin/sh
set -euo pipefail

./scripts/mkinitcpio.sh

grub-file --is-x86-multiboot2 image
if [[ $? -eq 1 ]]; then
    echo -e "Invalid multiboot2 header"
    exit
fi

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp image isodir/boot/image
cp initramfs isodir/boot/initramfs
cat >isodir/boot/grub/grub.cfg <<EOF
set timeout=0
set default=0
menuentry "nyxos" {
    multiboot2 /boot/image
    module2    /boot/initramfs "initramfs"
}
EOF

grub-mkrescue -o nyxos.iso isodir
