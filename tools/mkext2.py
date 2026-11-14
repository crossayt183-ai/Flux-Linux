"""Pure-Python ext2 filesystem image builder (4 KiB blocks, single block group)."""
import hashlib
import math
import struct
import sys
import zlib
from pathlib import Path

BLOCK_SIZE = 4096
INODE_SIZE = 128
SECTOR_SIZE = 512
EXT2_MAGIC = 0xEF53
EXT2_ROOT_INO = 2
EXT2_S_IFDIR = 0x4000
EXT2_S_IFREG = 0x8000
EXT2_FT_REG_FILE = 1
EXT2_FT_DIR = 2


class Ext2Image:
    def __init__(self, size_mb: int = 16, num_inodes: int = 512):
        self.total_blocks = (size_mb * 1024 * 1024) // BLOCK_SIZE
        self.num_inodes = num_inodes
        self.img = bytearray(self.total_blocks * BLOCK_SIZE)

        # Single block group layout (4 KiB blocks -> superblock starts at byte 1024 inside block 0)
        self.bgdt_blk = 1
        self.block_bmp_blk = 2
        self.inode_bmp_blk = 3
        self.inode_tbl_blk = 4
        inode_tbl_blks = math.ceil((num_inodes * INODE_SIZE) / BLOCK_SIZE)
        self.first_data_blk = self.inode_tbl_blk + inode_tbl_blks

        self.free_blocks = self.total_blocks - self.first_data_blk
        self.free_inodes = num_inodes - 10  # inodes 1..10 reserved
        self.used_dirs = 0
        self.next_free_blk = self.first_data_blk
        self.next_free_ino = 11

        # Reserve metadata blocks 0..(first_data_blk-1) in block bitmap
        for b in range(self.first_data_blk):
            self._set_bit(self.block_bmp_blk, b)
        # Reserve inodes 1..10 in inode bitmap (bit index ino-1)
        for ino in range(1, 11):
            self._set_bit(self.inode_bmp_blk, ino - 1)

        # Create root directory '/' at inode 2
        self.dir_entries: dict[int, list[tuple[int, int, bytes]]] = {}
        self.inodes: dict[int, dict] = {}
        self._init_dir(EXT2_ROOT_INO, EXT2_ROOT_INO)

    def _set_bit(self, bmp_blk: int, idx: int):
        off = bmp_blk * BLOCK_SIZE + (idx >> 3)
        self.img[off] |= (1 << (idx & 7))

    def _alloc_block(self) -> int:
        if self.next_free_blk >= self.total_blocks:
            raise RuntimeError("ext2 image out of blocks")
        b = self.next_free_blk
        self.next_free_blk += 1
        self._set_bit(self.block_bmp_blk, b)
        self.free_blocks -= 1
        return b

    def _alloc_inode(self) -> int:
        if self.next_free_ino > self.num_inodes:
            raise RuntimeError("ext2 image out of inodes")
        ino = self.next_free_ino
        self.next_free_ino += 1
        self._set_bit(self.inode_bmp_blk, ino - 1)
        self.free_inodes -= 1
        return ino

    def _init_dir(self, ino: int, parent_ino: int):
        self.used_dirs += 1
        self.inodes[ino] = {
            "mode": EXT2_S_IFDIR | 0o755,
            "size": 0,
            "links": 2 if ino == parent_ino else 2,
            "blocks": 0,
            "i_block": [0] * 15,
        }
        self.dir_entries[ino] = [
            (ino, EXT2_FT_DIR, b"."),
            (parent_ino, EXT2_FT_DIR, b".."),
        ]

    def ensure_dir(self, path: str) -> int:
        cur = EXT2_ROOT_INO
        for part in [p for p in path.split("/") if p]:
            bpart = part.encode("utf-8")
            found = None
            for child_ino, ftype, name in self.dir_entries[cur]:
                if name == bpart and ftype == EXT2_FT_DIR:
                    found = child_ino
                    break
            if found is None:
                found = self._alloc_inode()
                self._init_dir(found, cur)
                self.dir_entries[cur].append((found, EXT2_FT_DIR, bpart))
                self.inodes[cur]["links"] += 1
            cur = found
        return cur

    def add_file(self, path: str, data: bytes, executable: bool = False):
        parent_path, _, name = path.rpartition("/")
        dir_ino = self.ensure_dir(parent_path or "/")
        ino = self._alloc_inode()

        nblocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
        blk_list = []
        for i in range(nblocks):
            b = self._alloc_block()
            chunk = data[i * BLOCK_SIZE : (i + 1) * BLOCK_SIZE]
            self.img[b * BLOCK_SIZE : b * BLOCK_SIZE + len(chunk)] = chunk
            blk_list.append(b)

        i_block = [0] * 15
        for i, b in enumerate(blk_list[:12]):
            i_block[i] = b
        sectors = nblocks * (BLOCK_SIZE // SECTOR_SIZE)

        if len(blk_list) > 12:
            ind_blk = self._alloc_block()
            i_block[12] = ind_blk
            sectors += BLOCK_SIZE // SECTOR_SIZE
            for idx, b in enumerate(blk_list[12:]):
                struct.pack_into("<I", self.img, ind_blk * BLOCK_SIZE + idx * 4, b)

        mode = EXT2_S_IFREG | (0o755 if executable else 0o644)
        self.inodes[ino] = {
            "mode": mode,
            "size": len(data),
            "links": 1,
            "blocks": sectors,
            "i_block": i_block,
        }
        self.dir_entries[dir_ino].append((ino, EXT2_FT_REG_FILE, name.encode("utf-8")))

    def finalize(self) -> bytes:
        # Pack directories into allocated data blocks
        for dir_ino, entries in self.dir_entries.items():
            blocks_data = []
            cur_blk = bytearray(BLOCK_SIZE)
            off = 0
            for idx, (e_ino, ftype, name) in enumerate(entries):
                nlen = len(name)
                needed = ((8 + nlen + 3) // 4) * 4
                if off + needed > BLOCK_SIZE:
                    blocks_data.append(cur_blk)
                    cur_blk = bytearray(BLOCK_SIZE)
                    off = 0
                is_last_in_block = (idx + 1 == len(entries))
                # Look ahead if next doesn't fit
                if not is_last_in_block:
                    next_nlen = len(entries[idx + 1][2])
                    next_need = ((8 + next_nlen + 3) // 4) * 4
                    if off + needed + next_need > BLOCK_SIZE:
                        is_last_in_block = True
                rec_len = (BLOCK_SIZE - off) if is_last_in_block else needed
                struct.pack_into("<IHBB", cur_blk, off, e_ino, rec_len, nlen, ftype)
                cur_blk[off + 8 : off + 8 + nlen] = name
                off += rec_len
            blocks_data.append(cur_blk)

            meta = self.inodes[dir_ino]
            meta["size"] = len(blocks_data) * BLOCK_SIZE
            meta["blocks"] = len(blocks_data) * (BLOCK_SIZE // SECTOR_SIZE)
            for bi, bd in enumerate(blocks_data):
                pb = self._alloc_block()
                self.img[pb * BLOCK_SIZE : (pb + 1) * BLOCK_SIZE] = bd
                meta["i_block"][bi] = pb

        # Write inodes (40-byte fixed header + 60-byte i_block[15])
        assert struct.calcsize("<HHIIIIIHHIII") == 40
        for ino, m in self.inodes.items():
            off = self.inode_tbl_blk * BLOCK_SIZE + (ino - 1) * INODE_SIZE
            args = (
                m["mode"], 0, m["size"],
                0, 0, 0, 0,
                0, m["links"], m["blocks"],
                0, 0,
            )
            struct.pack_into("<HHIIIIIHHIII", self.img, off, *args)
            for idx in range(15):
                struct.pack_into("<I", self.img, off + 40 + idx * 4, m["i_block"][idx])

        # Write BGD at block 1
        struct.pack_into(
            "<IIIHHH",
            self.img, self.bgdt_blk * BLOCK_SIZE,
            self.block_bmp_blk, self.inode_bmp_blk, self.inode_tbl_blk,
            self.free_blocks, self.free_inodes, self.used_dirs,
        )

        # Write superblock at byte offset 1024
        sb_off = 1024
        struct.pack_into(
            "<IIIIIII",
            self.img, sb_off,
            self.num_inodes, self.total_blocks, 0,
            self.free_blocks, self.free_inodes,
            0, 2,  # s_first_data_block=0, s_log_block_size=2 -> 4096B
        )
        struct.pack_into("<III", self.img, sb_off + 28, 2, self.total_blocks, self.total_blocks)
        struct.pack_into("<I", self.img, sb_off + 40, self.num_inodes)
        struct.pack_into("<HHHH", self.img, sb_off + 52, 1, 0, EXT2_MAGIC, 1)
        struct.pack_into("<IIHH", self.img, sb_off + 72, 0, 1, 0, 0)
        struct.pack_into("<IH", self.img, sb_off + 84, 11, INODE_SIZE)
        return bytes(self.img)


def build_disk_image(out_path: Path, bin_files: list[tuple[str, Path]]):
    fs = Ext2Image(size_mb=16, num_inodes=512)
    fs.ensure_dir("/bin")
    fs.ensure_dir("/boot")
    fs.ensure_dir("/etc")
    fs.ensure_dir("/home")
    fs.ensure_dir("/tmp")
    fs.add_file("/boot/version", b"FluxOS 0.4.0 x86_64 Limine UEFI + SecStack\n")
    fs.add_file("/etc/motd", b"FluxOS x86_64 v0.4 -- SELinux + LUKS2 + Wayland + Flatpak\n")
    fs.add_file("/etc/hostname", b"fluxbox\n")
    fs.add_file("/etc/flux.conf", b"hostname=fluxbox\nfs=ext2\nip=10.0.2.15\ngw=10.0.2.2\n")
    fs.add_file("/etc/selinux.conf", b"SELINUX=enforcing\nSELINUXTYPE=targeted\n")
    fs.add_file("/etc/flatpak.db", (
        b"org.flux.Hello /bin/hello stdio\n"
        b"org.flux.NetCheck /bin/ping stdio,net\n"
        b"org.flux.WaylandDemo /bin/wldemo stdio,wayland\n"
    ))
    fs.add_file("/home/readme.txt", b"Welcome to FluxOS! Real ext2 R/W storage over Intel AHCI DMA.\n")
    wall_zz = Path(__file__).resolve().parent.parent / "assets" / "wall_1024x768.rgb.zz"
    if wall_zz.exists():
        fs.add_file("/etc/wall.rgb", zlib.decompress(wall_zz.read_bytes()))
    secboot_lines = ["# FluxOS UEFI SecureBoot SHA-256 Manifest\n"]
    for guest_path, host_path in bin_files:
        raw = host_path.read_bytes()
        fs.add_file(guest_path, raw, executable=True)
        digest = hashlib.sha256(raw).hexdigest()
        secboot_lines.append(f"{guest_path} {digest}\n")
    fs.add_file("/etc/secboot.db", "".join(secboot_lines).encode("ascii"))
    out_path.write_bytes(fs.finalize())


if __name__ == "__main__":
    out = Path(sys.argv[1])
    bins = []
    for arg in sys.argv[2:]:
        gpath, hpath = arg.split("=", 1)
        bins.append((gpath, Path(hpath)))
    build_disk_image(out, bins)
    print(f"[MKEXT2] Generated {out} ({out.stat().st_size // (1024*1024)} MiB, {len(bins)} ELF bins)")
