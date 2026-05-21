# Leopard Coded File Demo

Minimal example showing how to use Leopard-RS to encode one file into:

```text
[header][k padded data blocks][p parity blocks]
```

Then decode after known block erasures. Erasures are flagged by passing `NULL`
for missing `original_data[i]` or `recovery_data[j]` pointers.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target coded_file_demo --parallel
```

## Example

This uses this `README.md` as the input file, erases data block `0`, recovers it
from parity, and verifies the recovered file.

```sh
mkdir -p build/file-demo

./build/coded_file_demo encode README.md build/file-demo/readme.leo 1 1048576
./build/coded_file_demo decode build/file-demo/readme.leo build/file-demo/README.recovered.md --erase-data 0
cmp README.md build/file-demo/README.recovered.md
```

Output:

```text
Encoded k=1 data blocks and p=1 parity blocks
Block size: 1048576 bytes
Wrote coded file: build/file-demo/readme.leo
Decoded build/file-demo/README.recovered.md
Erased data blocks: 0
Erased parity blocks: none
```

Or run:

```sh
./examples/run_coded_file_demo.sh
```

## API Notes

- `block_bytes` must be a multiple of 64. This demo uses 1 MiB.
- Pad the final data block with zeroes before encoding.
- `p <= k` is required by this implementation; `p == k` is allowed.
- `k + p <= 65536`.
- Leopard is an erasure code; the caller must know which blocks are missing.
