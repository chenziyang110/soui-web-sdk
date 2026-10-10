/* Minimal zlib uncompress entry point kept beside the bundled inflate units.
 * The source package intentionally carries the C translation units instead of
 * linking a platform zlib DLL, so the native workload and bridge smoke builds
 * remain reproducible offline. */
#include "zlib.h"

int ZEXPORT uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen) {
    z_stream stream;
    int err;
    const uInt max = (uInt)-1;
    uLong left;

    if (destLen == Z_NULL) return Z_STREAM_ERROR;
    stream.next_in = (z_const Bytef *)source;
    stream.avail_in = 0;
    stream.zalloc = (alloc_func)0;
    stream.zfree = (free_func)0;
    stream.opaque = (voidpf)0;
    err = inflateInit(&stream);
    if (err != Z_OK) return err;

    stream.next_out = dest;
    stream.avail_out = 0;
    left = *destLen;
    do {
        if (stream.avail_out == 0) {
            stream.avail_out = left > (uLong)max ? max : (uInt)left;
            left -= stream.avail_out;
        }
        if (stream.avail_in == 0) {
            stream.avail_in = sourceLen > (uLong)max ? max : (uInt)sourceLen;
            sourceLen -= stream.avail_in;
        }
        err = inflate(&stream, Z_NO_FLUSH);
    } while (err == Z_OK);

    *destLen = stream.total_out;
    inflateEnd(&stream);
    return err == Z_STREAM_END ? Z_OK : err == Z_NEED_DICT ? Z_DATA_ERROR : err;
}
