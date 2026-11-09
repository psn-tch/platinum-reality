# Live initramfs size: preserve hardware payloads

The live profile uses supported `MODULES_DECOMPRESS="yes"` and xz preset 9e,
CRC32, with four compression workers requested. The installed-target boot
policy remains separate. No firmware, kernel module, CPU microcode, application
or package was removed to achieve this change.

The earlier image's 230 MB early CPIO contained about 18 MB of microcode,
154 MB of compressed firmware and 58 MB of compressed modules. mkinitcpio moved
already-compressed files into that early archive to avoid double compression.
Its supported decompression option allows the module/firmware contents to be
compressed together in the main archive instead. The real microcode remains
uncompressed early, as required by the [Linux microcode loader](https://www.kernel.org/doc/html/v6.2/x86/microcode.html).

## Measurement, not image acceptance

An owned 8 GiB QEMU builder regenerated the initramfs from the verified immutable
root of image `d894730f051ecab2d22ef578ee74ccac32a55086cf4a4f5c5a65ce08bd8383fe`,
using its actual tools, kernel and unchanged hook list. Original initramfs:
256,079,164 bytes. The selected setting produced 204,587,664 bytes in 131.885
seconds, saving 51,491,500 bytes. Every named logical payload matched its
original decompressed SHA256: 1,475 modules, 1,147 firmware files and both CPU
microcode blobs; no missing, extra or changed payloads.

Additional same-CPIO recompression kept the exact early bytes and main decoded
SHA256. A single 64 MiB dictionary stream produced 203,672,680 combined bytes
in 245.476 seconds. A 128 MiB dictionary/512 MiB block setting produced
198,708,140 bytes in 245.817 seconds. The selected simpler 64 MiB/four-worker
setting retains faster observed build time and lower decoder memory; the extra
5.9 MB saving of the larger dictionary did not meet the softer 1.95e9-byte ISO
aim either. These are one-machine compression observations, not boot timings
or a guarantee of worker utilization.

The same-payload projection is about 1.970e9 bytes, below the required actual
2 GB USB capacity of 1,992,294,400 bytes. It is above the 1,950,000,000-byte aim.
**Only the newly built ISO's measured size/hash and complete boot/install/login
qualification can establish acceptance.** New package versions may change size.
The independent USB-capacity release gate must refuse an oversized candidate;
no media write is authorized by a compression result.
