( () => {
    var e = {
        8226: e => {
            function t(e) {
                return Promise.resolve().then(( () => {
                    var t = new Error("Cannot find module '" + e + "'");
                    throw t.code = "MODULE_NOT_FOUND",
                    t
                }
                ))
            }
            t.keys = () => [],
            t.resolve = t,
            t.id = 8226,
            e.exports = t
        }
    }
      , t = {};
    function r(a) {
        var s = t[a];
        if (void 0 !== s)
            return s.exports;
        var o = t[a] = {
            exports: {}
        };
        return e[a](o, o.exports, r),
        o.exports
    }
    r.o = (e, t) => Object.prototype.hasOwnProperty.call(e, t),
    r.gca = function(e) {
        return e = {}[e] || e,
        r.p + r.u(e)
    }
    ,
    ( () => {
        "use strict";
        const e = "https://unpkg.com/@ffmpeg/core@0.12.10/dist/umd/ffmpeg-core.js";
        var t;
        !function(e) {
            e.LOAD = "LOAD",
            e.EXEC = "EXEC",
            e.FFPROBE = "FFPROBE",
            e.WRITE_FILE = "WRITE_FILE",
            e.READ_FILE = "READ_FILE",
            e.DELETE_FILE = "DELETE_FILE",
            e.RENAME = "RENAME",
            e.CREATE_DIR = "CREATE_DIR",
            e.LIST_DIR = "LIST_DIR",
            e.DELETE_DIR = "DELETE_DIR",
            e.ERROR = "ERROR",
            e.DOWNLOAD = "DOWNLOAD",
            e.PROGRESS = "PROGRESS",
            e.LOG = "LOG",
            e.MOUNT = "MOUNT",
            e.UNMOUNT = "UNMOUNT"
        }(t || (t = {}));
        const a = new Error("unknown message type")
          , s = new Error("ffmpeg is not loaded, call `await ffmpeg.load()` first")
          , o = (new Error("called FFmpeg.terminate()"),
        new Error("failed to import ffmpeg-core.js"));
        let n;
        self.onmessage = async E => {
            let {data: {id: c, type: i, data: p}} = E;
            const l = [];
            let R;
            try {
                if (i !== t.LOAD && !n)
                    throw s;
                switch (i) {
                case t.LOAD:
                    R = await (async a => {
                        let {coreURL: s, wasmURL: E, workerURL: c} = a;
                        const i = !n;
                        try {
                            s || (s = e),
                            importScripts(s)
                        } catch {
                            if (s && s !== e || (s = e.replace("/umd/", "/esm/")),
                            self.createFFmpegCore = (await r(8226)(s)).default,
                            !self.createFFmpegCore)
                                throw o
                        }
                        const p = s
                          , l = E || s.replace(/.js$/g, ".wasm")
                          , R = c || s.replace(/.js$/g, ".worker.js");
                        return n = await self.createFFmpegCore({
                            mainScriptUrlOrBlob: `${p}#${btoa(JSON.stringify({
                                wasmURL: l,
                                workerURL: R
                            }))}`
                        }),
                        n.setLogger((e => self.postMessage({
                            type: t.LOG,
                            data: e
                        }))),
                        n.setProgress((e => self.postMessage({
                            type: t.PROGRESS,
                            data: e
                        }))),
                        i
                    }
                    )(p);
                    break;
                case t.EXEC:
                    R = (e => {
                        let {args: t, timeout: r=-1} = e;
                        n.setTimeout(r),
                        n.exec(...t);
                        const a = n.ret;
                        return n.reset(),
                        a
                    }
                    )(p);
                    break;
                case t.FFPROBE:
                    R = (e => {
                        let {args: t, timeout: r=-1} = e;
                        n.setTimeout(r),
                        n.ffprobe(...t);
                        const a = n.ret;
                        return n.reset(),
                        a
                    }
                    )(p);
                    break;
                case t.WRITE_FILE:
                    R = (e => {
                        let {path: t, data: r} = e;
                        return n.FS.writeFile(t, r),
                        !0
                    }
                    )(p);
                    break;
                case t.READ_FILE:
                    R = (e => {
                        let {path: t, encoding: r} = e;
                        return n.FS.readFile(t, {
                            encoding: r
                        })
                    }
                    )(p);
                    break;
                case t.DELETE_FILE:
                    R = (e => {
                        let {path: t} = e;
                        return n.FS.unlink(t),
                        !0
                    }
                    )(p);
                    break;
                case t.RENAME:
                    R = (e => {
                        let {oldPath: t, newPath: r} = e;
                        return n.FS.rename(t, r),
                        !0
                    }
                    )(p);
                    break;
                case t.CREATE_DIR:
                    R = (e => {
                        let {path: t} = e;
                        return n.FS.mkdir(t),
                        !0
                    }
                    )(p);
                    break;
                case t.LIST_DIR:
                    R = (e => {
                        let {path: t} = e;
                        const r = n.FS.readdir(t)
                          , a = [];
                        for (const s of r) {
                            const e = n.FS.stat(`${t}/${s}`)
                              , r = n.FS.isDir(e.mode);
                            a.push({
                                name: s,
                                isDir: r
                            })
                        }
                        return a
                    }
                    )(p);
                    break;
                case t.DELETE_DIR:
                    R = (e => {
                        let {path: t} = e;
                        return n.FS.rmdir(t),
                        !0
                    }
                    )(p);
                    break;
                case t.MOUNT:
                    R = (e => {
                        let {fsType: t, options: r, mountPoint: a} = e;
                        const s = t
                          , o = n.FS.filesystems[s];
                        return !!o && (n.FS.mount(o, r, a),
                        !0)
                    }
                    )(p);
                    break;
                case t.UNMOUNT:
                    R = (e => {
                        let {mountPoint: t} = e;
                        return n.FS.unmount(t),
                        !0
                    }
                    )(p);
                    break;
                default:
                    throw a
                }
            } catch (u) {
                return void self.postMessage({
                    id: c,
                    type: t.ERROR,
                    data: u.toString()
                })
            }
            R instanceof Uint8Array && l.push(R.buffer),
            self.postMessage({
                id: c,
                type: i,
                data: R
            }, l)
        }
    }
    )()
}
)();
