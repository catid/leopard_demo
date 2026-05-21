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

This uses the included `examples/demo_video.mp4` file as input, splits it into
1280-byte data blocks, erases known data/parity blocks, recovers the missing
data, and verifies the recovered file.

```sh
mkdir -p build/file-demo

./build/coded_file_demo encode examples/demo_video.mp4 build/file-demo/demo_video.leo 4 1280
./build/coded_file_demo decode build/file-demo/demo_video.leo build/file-demo/demo_video.recovered.mp4 --erase-data 0,127,1226 --erase-parity 1
cmp examples/demo_video.mp4 build/file-demo/demo_video.recovered.mp4
./build/coded_file_demo fuzz examples/demo_video.mp4 build/file-demo/demo_video.leo 10000 12345
```

Output:

```text
Encoded k=1227 data blocks and p=4 parity blocks
Block size: 1280 bytes
Wrote coded file: build/file-demo/demo_video.leo
Symbol encode throughput: 10676.75 MB/s
Decoded build/file-demo/demo_video.recovered.mp4
Erased data blocks: 0 127 1226
Erased parity blocks: 1
Symbol decode throughput: 2034.43 MB/s
Fuzz trials: 10000
Source: examples/demo_video.mp4
Coded file: build/file-demo/demo_video.leo
Parameters: k=1227 p=4 block_bytes=1280 seed=12345
Example loss patterns:
  trial 0: data=[840,295,707] parity=[0]
  trial 1: data=[504,308] parity=[2,1]
  trial 2: data=[939,223] parity=[]
  trial 3: data=[455,264] parity=[3,2]
  trial 4: data=[136,554] parity=[3,1]
  trial 5: data=[695,540] parity=[]
  trial 6: data=[1094,1203,973,125] parity=[]
  trial 7: data=[568,968] parity=[1]
  trial 8: data=[330,227,933,313] parity=[]
  trial 9: data=[1141,532] parity=[3,2]
Elapsed: 6896 ms
Result: PASS
```

Or run:

```sh
./examples/run_coded_file_demo.sh
```

## API Notes

- `block_bytes` must be a multiple of 64. This demo uses 1280 bytes.
- Pad the final data block with zeroes before encoding.
- `p <= k` is required by this implementation; `p == k` is allowed.
- `k + p <= 65536`.
- Leopard is an erasure code; the caller must know which blocks are missing.
- The fuzz test requires explicit parameters.
- Throughput measures only `leo_encode`/`leo_decode` symbol processing.
