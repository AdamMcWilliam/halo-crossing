"""Read-only access to an Xbox game disc image (XDVDFS / "xiso").

Files are read in place from the image; nothing is copied. Handles trimmed
xiso images (game partition at offset 0) and full redump images (game
partition at a fixed offset).
"""
import struct

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Volume descriptor sits at sector 32 of the game partition.
_PARTITION_OFFSETS = (0, 0x18300000, 0xFD90000, 0x2080000)
ATTR_DIRECTORY = 0x10


class XboxDisc:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.base = None
        for off in _PARTITION_OFFSETS:
            self.f.seek(off + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                self.base = off
                break
        if self.base is None:
            raise ValueError(f"{path}: not an Xbox disc image (no XDVDFS volume descriptor)")
        self.f.seek(self.base + 32 * SECTOR + 20)
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))
        self._index = None

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read(self, sector, size, offset=0):
        self.f.seek(self.base + sector * SECTOR + offset)
        return self.f.read(size)

    def _walk_dir(self, sector, size, prefix, out):
        if size == 0:
            return
        table = self._read(sector, size)
        stack = [0]
        while stack:
            off = stack.pop() * 4
            if off + 14 > len(table):
                continue
            left, right, start, length, attr, name_len = struct.unpack_from("<HHIIBB", table, off)
            if left == 0xFFFF:  # padding / empty table
                continue
            name = table[off + 14:off + 14 + name_len].decode("latin-1")
            path = f"{prefix}{name}"
            if attr & ATTR_DIRECTORY:
                out[path.lower() + "/"] = (start, length, True, path)
                self._walk_dir(start, length, path + "/", out)
            else:
                out[path.lower()] = (start, length, False, path)
            if left:
                stack.append(left)
            if right:
                stack.append(right)

    def index(self):
        if self._index is None:
            self._index = {}
            self._walk_dir(self.root_sector, self.root_size, "", self._index)
        return self._index

    def files(self, prefix=""):
        """(path, size) for every file under prefix, case-insensitive."""
        p = prefix.lower()
        return sorted((v[3], v[1]) for k, v in self.index().items() if not v[2] and k.startswith(p))

    def read(self, path):
        entry = self.index().get(path.lower())
        if entry is None or entry[2]:
            raise FileNotFoundError(path)
        return self._read(entry[0], entry[1])
