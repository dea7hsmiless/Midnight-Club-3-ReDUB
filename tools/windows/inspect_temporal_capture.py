"""Inspect ReDUB temporal/NGX diagnostic captures using only Python stdlib."""
import argparse, json, math, pathlib, struct, zlib, binascii

def read(path):
    data = path.read_bytes()
    if data[:8] == b'RDMV0001':
        w, h, frame = struct.unpack_from('<III', data, 8)
        fmt, offset = 97, 20  # VK_FORMAT_R16G16B16A16_SFLOAT
    elif data[:8] == b'RDIM0001':
        w, h, fmt, frame = struct.unpack_from('<IIII', data, 8)
        offset = 24
    else:
        raise ValueError(f'{path}: unknown header')
    layouts = {97: '<4e', 100: '<f', 83: '<2e'}
    layout = layouts[fmt]
    if len(data) - offset != w * h * struct.calcsize(layout):
        raise ValueError(f'{path}: payload size mismatch')
    values = list(struct.iter_unpack(layout, data[offset:]))
    if any(not math.isfinite(v) for row in values for v in row):
        raise ValueError(f'{path}: nonfinite pixels')
    return w, h, fmt, frame, values

def png(path, w, h, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)
    pixels = bytearray()
    for i, row in enumerate(rows):
        if i % w == 0:
            pixels.append(0)
        pixels.extend(max(0, min(255, round(c * 255))) for c in row[:3])
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=pathlib.Path)
    parser.add_argument('--previews', action='store_true')
    args = parser.parse_args()
    report = {}
    for name in ('motion', 'scene-color', 'scene-depth', 'scene-motion', 'scene-reject', 'dlss-output', 'scene-composed'):
        path = pathlib.Path(str(args.prefix) + '-' + name + '.bin')
        if not path.exists():
            continue
        w, h, fmt, frame, rows = read(path)
        item = {'width': w, 'height': h, 'format': fmt, 'frame': frame, 'pixels': len(rows), 'finite': True}
        if name == 'motion':
            valid = [row for row in rows if row[2] < .5]
            mags = sorted(math.hypot(row[0], row[1]) for row in valid)
            item.update(valid_percent=100 * len(valid) / len(rows), moving_pixels=sum(m > .01 for m in mags), motion_median=mags[len(mags)//2] if mags else None)
        elif name in ('scene-depth', 'scene-reject'):
            item.update(min=min(row[0] for row in rows), max=max(row[0] for row in rows))
            if item['min'] < 0 or item['max'] > 1:
                raise ValueError(f'{path}: normalized scalar outside [0,1]')
        elif name in ('scene-color', 'dlss-output', 'scene-composed'):
            item.update(alpha_min=min(row[3] for row in rows), alpha_max=max(row[3] for row in rows), mean_rgb=[sum(row[c] for row in rows) / len(rows) for c in range(3)], nonblack_pixels=sum(any(c > .001 for c in row[:3]) for row in rows))
            if not item['nonblack_pixels']:
                raise ValueError(f'{path}: empty color output')
            if args.previews:
                png(path.with_suffix('.png'), w, h, rows)
        report[name] = item
    if not report:
        raise ValueError('No captures found for this prefix')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
