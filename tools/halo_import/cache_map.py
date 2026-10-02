"""Halo CE Xbox cache files (maps/*.map, engine build 01.10.12.2276).

Layout: a 2048-byte header ('head' ... 'foot', little-endian), then the rest
of the file zlib-compressed. Tag data is loaded at a fixed Xbox virtual
address, so every pointer inside it is converted with `addr()`.
"""
import struct
import zlib

HEADER_SIZE = 2048
XBOX_VERSION = 5
TAG_BASE = 0x803A6000
NULL_ID = 0xFFFFFFFF


def fourcc(v):
    return struct.pack(">I", v).decode("latin-1")


class Tag:
    __slots__ = ("group", "parents", "id", "path", "data", "index")

    def __init__(self, group, parents, tag_id, path, data, index):
        self.group, self.parents, self.id = group, parents, tag_id
        self.path, self.data, self.index = path, data, index

    def __repr__(self):
        return f"<{self.group} {self.path}>"


class CacheMap:
    def __init__(self, raw, name=""):
        head = raw[:HEADER_SIZE]
        if head[:4] != b"daeh" or head[0x7FC:0x800] != b"toof":
            raise ValueError(f"{name}: not a Halo cache file")
        version, self.file_size, _, self.tag_data_offset, self.tag_data_size = struct.unpack_from("<5I", head, 4)
        if version != XBOX_VERSION:
            raise ValueError(f"{name}: cache version {version}, expected Xbox ({XBOX_VERSION})")
        self.name = head[0x20:0x40].split(b"\0", 1)[0].decode("ascii")
        self.build = head[0x40:0x60].split(b"\0", 1)[0].decode("ascii")
        self.map_type = struct.unpack_from("<H", head, 0x60)[0]
        body = raw[HEADER_SIZE:]
        if body[:1] == b"\x78":
            body = zlib.decompress(body)
        self.m = memoryview(head + body)
        if len(self.m) != self.file_size:
            raise ValueError(f"{name}: decompressed to {len(self.m)} bytes, header says {self.file_size}")
        self._read_tags()

    # -- addressing ---------------------------------------------------------
    def addr(self, ptr):
        """File offset of an Xbox tag-data pointer."""
        return ptr - TAG_BASE + self.tag_data_offset

    def u32(self, off):
        return struct.unpack_from("<I", self.m, off)[0]

    def unpack(self, fmt, off):
        return struct.unpack_from("<" + fmt, self.m, off)

    def cstring(self, off):
        end = off
        while self.m[end]:
            end += 1
        return bytes(self.m[off:end]).decode("latin-1")

    # -- tag index ----------------------------------------------------------
    def _read_tags(self):
        o = self.tag_data_offset
        (array_ptr, self.scenario_id, self.checksum, count, self.model_part_count, self.vertex_ptr,
         _, self.index_ptr, sig) = self.unpack("9I", o)
        if fourcc(sig) != "tags":
            raise ValueError(f"{self.name}: bad tag data signature")
        self.tags = []
        self.by_id = {}
        base = self.addr(array_ptr)
        for i in range(count):
            g0, g1, g2, tag_id, name_ptr, data_ptr, _, _ = self.unpack("8I", base + i * 32)
            tag = Tag(fourcc(g0), (fourcc(g1), fourcc(g2)), tag_id, self.cstring(self.addr(name_ptr)),
                      data_ptr, i)
            self.tags.append(tag)
            self.by_id[tag_id] = tag

    def find(self, group, path):
        p = path.lower()
        for t in self.tags:
            if t.group == group and t.path.lower() == p:
                return t
        return None

    def of_group(self, group):
        return [t for t in self.tags if t.group == group]

    # -- tag primitives -----------------------------------------------------
    def tag_data(self, tag):
        return self.addr(tag.data)

    def ref(self, off):
        """Tag reference (16 bytes): group, path ptr, path length, id."""
        group, _, _, tag_id = self.unpack("4I", off)
        return self.by_id.get(tag_id) if tag_id != NULL_ID else None

    def block(self, off):
        """Tag block / reflexive (12 bytes): count, pointer, definition."""
        count, ptr, _ = self.unpack("3I", off)
        return count, (self.addr(ptr) if count else 0)

    def data_ref(self, off):
        """Data reference (20 bytes): size, flags, file offset, pointer, definition."""
        size, flags, file_off, ptr, _ = self.unpack("5I", off)
        return size, (self.addr(ptr) if ptr else file_off)


def load_from_disc(disc, map_name):
    return CacheMap(disc.read(f"maps/{map_name}.map"), map_name)
