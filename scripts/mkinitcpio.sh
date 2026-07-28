#! /bin/sh
set -euo pipefail

mkdir -p tmp/initcpio
cat >tmp/initcpio/testfile.txt <<EOF
Welcome kernel.
This data was written from a file.
To prove it, here's a magic value:
6202145682195251
EOF

mkdir -p tmp/initcpio/test/path/a
cat >tmp/initcpio/test/path/a/testfile.txt <<EOF
Welcome kernel.
This data was written from a file.
To prove it, here's a magic value:
6202145682195251
EOF

mkdir -p tmp/initcpio/dev

cd tmp/initcpio
find . | cpio -o -H newc >../../initramfs
