// Example for making work on wasm without libc
const fds = new Map();
let nextFd = 1;
let memory;

const decoder = new TextDecoder('utf-8');
const virtualFs = new Map();

function u8(ptr, len) {
    return new Uint8Array(memory.buffer, ptr, len);
}

function readStr(ptr, len) {
    return decoder.decode(u8(ptr, len));
}

const env = {
    console_log(ptr, len) {
        console.log(readStr(ptr, len));
    },

    fs_open(namePtr, nameLen, access) {
        const name = readStr(namePtr, nameLen);

        if (access === 0 /* RD */) {
            const data = virtualFs.get(name);
            if (!data) return -1;
            const fd = nextFd++;
            fds.set(fd, { name, data, write: false });
            return fd;
        } else /* WR */ {
            const fd = nextFd++;
            const data = new Uint8Array(0);
            fds.set(fd, { name, data, write: true });
            virtualFs.set(name, data);
            return fd;
        }
    },

    fs_close(fd) { fds.delete(fd); },

    fs_read(fd, offset, bufPtr, len) {
        const f = fds.get(fd);
        if (!f) return 0;
        const off = Number(offset);
        const slice = f.data.subarray(off, off + len);
        u8(bufPtr, slice.length).set(slice);
        return slice.length;
    },

    fs_write(fd, offset, bufPtr, len) {
        const f = fds.get(fd);
        if (!f) return 0;
        const off = Number(offset);
        const need = off + len;
        if (f.data.length < need) {
            const grown = new Uint8Array(need);
            grown.set(f.data);
            f.data = grown;
            virtualFs.set(f.name, grown);
        }
        f.data.set(u8(bufPtr, len), off);
        return len;
    },

    fs_size(fd) {
        const f = fds.get(fd);
        return f ? BigInt(f.data.length) : 0n;
    },

    fs_opendir() { return -1; },
    fs_closedir() {},
    fs_readdir() { return 0; },
};

export async function loadWasm(url = 'app.wasm') {
    const { instance } = await WebAssembly.instantiateStreaming(
        fetch(url),
        { env }
    );
    memory = instance.exports.memory;
    return instance;
}

export function vfsPut(name, data) {
    if (typeof data === 'string') {
        data = new TextEncoder().encode(data);
    }
    virtualFs.set(name, data);
}

export function vfsGet(name) {
    return virtualFs.get(name);
}
