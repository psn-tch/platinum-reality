"""Read-only access to classic HFS volumes ("Mac OS Standard", as on Mac OS 9 install CDs).

7-Zip reads HFS+ but not the older HFS that many CDs use. This reads the volume's catalog and
extents B-trees directly from an image (a bare volume, or a disk or CD with an Apple partition
map) and returns files' data and resource forks. It never writes.
"""
import struct

SECTOR = 512


class HFSError(Exception):
    pass


def hfs_offsets(read):
    """Byte offsets of the classic HFS volumes in an image: a bare volume, or Apple_HFS partitions."""
    if read(1024, 2) == b'BD':
        return [0]
    found = []
    if read(0, 2) == b'ER':
        for index in range(1, 64):
            entry = read(index * SECTOR, SECTOR)
            if len(entry) < 64 or entry[:2] != b'PM':
                break
            start, = struct.unpack_from('>I', entry, 8)
            kind = entry[48:80].split(b'\0', 1)[0]
            if kind == b'Apple_HFS' and read(start * SECTOR + 1024, 2) == b'BD':
                found.append(start * SECTOR)
    return found


class Volume:
    def __init__(self, read, offset=0):
        self._read = read
        self._offset = offset
        mdb = read(offset + 1024, 162)
        if mdb[:2] != b'BD':
            raise HFSError('not a classic HFS volume')
        self.block_size, = struct.unpack_from('>I', mdb, 20)
        first_block, = struct.unpack_from('>H', mdb, 28)
        self._start = offset + first_block * SECTOR
        self.name = mdb[37:37 + mdb[36]].decode('mac_roman')
        extents_size, = struct.unpack_from('>I', mdb, 130)
        catalog_size, = struct.unpack_from('>I', mdb, 146)
        self._overflow = {}
        extents_tree = self._fork(self._extents(mdb[134:146]), extents_size)
        for key, data in self._leaves(extents_tree):
            if len(key) >= 8:
                fork_type, file_id, start = struct.unpack_from('>BIH', key, 1)
                self._overflow[(file_id, fork_type, start)] = self._extents(data[:12])
        catalog = self._fork(self._extents(mdb[150:162]), catalog_size)
        folders, self.files = {}, {}
        for key, data in self._leaves(catalog):
            parent, = struct.unpack_from('>I', key, 2)
            name = key[7:7 + key[6]].decode('mac_roman')
            if data[0] == 1:
                folder, = struct.unpack_from('>I', data, 6)
                folders[folder] = (parent, name)
            elif data[0] == 2:
                file_id, = struct.unpack_from('>I', data, 20)
                data_length, = struct.unpack_from('>I', data, 26)
                resource_length, = struct.unpack_from('>I', data, 36)
                self.files[(parent, name)] = (file_id, data_length, self._extents(data[74:86]),
                                              resource_length, self._extents(data[86:98]))
        self._folders = folders

    def _extents(self, record):
        return [struct.unpack_from('>HH', record, 4 * i) for i in range(3) if struct.unpack_from('>HH', record, 4 * i)[1]]

    def _fork(self, extents, length, file_id=None, fork_type=0):
        out, done = bytearray(), 0
        extents = list(extents)
        while len(out) < length:
            if not extents:
                more = self._overflow.get((file_id, fork_type, done)) if file_id is not None else None
                if not more:
                    raise HFSError('fork runs past its extents')
                extents = list(more)
            start, count = extents.pop(0)
            out += self._read(self._start + start * self.block_size, count * self.block_size)
            done += count
        return bytes(out[:length])

    def _leaves(self, tree):
        """(key, record) for every leaf record of a B-tree held in `tree`."""
        node_size, = struct.unpack_from('>H', tree, 32)
        leaf, = struct.unpack_from('>I', tree, 24)
        seen = set()
        while leaf and leaf not in seen:
            seen.add(leaf)
            node = tree[leaf * node_size:(leaf + 1) * node_size]
            if len(node) < node_size:
                raise HFSError('B-tree node out of range')
            following, = struct.unpack_from('>I', node, 0)
            count, = struct.unpack_from('>H', node, 10)
            for index in range(count):
                at, = struct.unpack_from('>H', node, node_size - 2 * (index + 1))
                key_length = node[at]
                key = node[at:at + 1 + key_length]
                body = at + 1 + key_length
                body += body & 1
                yield key, node[body:]
            leaf = following

    def path(self, folder):
        parts = []
        while folder in self._folders and folder != 1:
            folder, name = self._folders[folder][0], self._folders[folder][1]
            parts.append(name)
        return '/'.join(reversed(parts))

    def listing(self):
        """Every file as 'Volume/Folder/.../Name' -> its key."""
        return {self.path(parent) + '/' + name: (parent, name) for parent, name in self.files}

    def read(self, key, resource=False):
        file_id, data_length, data_extents, resource_length, resource_extents = self.files[key]
        if resource:
            return self._fork(resource_extents, resource_length, file_id, 0xFF)
        return self._fork(data_extents, data_length, file_id, 0)


def open_image(path):
    """The classic HFS volumes in the image at `path` (empty when it has none)."""
    handle = open(path, 'rb')

    def read(at, length):
        handle.seek(at)
        return handle.read(length)
    return [Volume(read, offset) for offset in hfs_offsets(read)]
