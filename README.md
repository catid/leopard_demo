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

This uses the included `examples/demo_video.mp4` file as input, erases data
block `0`, recovers it from parity, and verifies the recovered file.

```sh
mkdir -p build/file-demo

./build/coded_file_demo encode examples/demo_video.mp4 build/file-demo/demo_video.leo 2 1048576
./build/coded_file_demo decode build/file-demo/demo_video.leo build/file-demo/demo_video.recovered.mp4 --erase-data 0
cmp examples/demo_video.mp4 build/file-demo/demo_video.recovered.mp4
./build/coded_file_demo fuzz 10000 8 4 4096 12345
```

Output:

```text
Encoded k=2 data blocks and p=2 parity blocks
Block size: 1048576 bytes
Wrote coded file: build/file-demo/demo_video.leo
File encode throughput: <varies> MB/s
Decoded build/file-demo/demo_video.recovered.mp4
Erased data blocks: 0
Erased parity blocks: none
File decode throughput: <varies> MB/s
Fuzz trials: 10000
Parameters: k=8 p=4 block_bytes=4096 seed=12345
Example loss patterns:
  trial 0: data=[4,1] parity=[1,2]
  trial 1: data=[5] parity=[1,0,2]
  trial 2: data=[2] parity=[1,0]
  trial 3: data=[6,1,3] parity=[2]
  trial 4: data=[0] parity=[1]
  trial 5: data=[4,2] parity=[0,2]
  trial 6: data=[1,5,3,7] parity=[]
  trial 7: data=[4,3,6] parity=[1]
  trial 8: data=[6] parity=[2,3]
  trial 9: data=[4,3,2,1] parity=[]
Elapsed: <varies> ms
Result: PASS
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
- The fuzz test requires explicit parameters.
