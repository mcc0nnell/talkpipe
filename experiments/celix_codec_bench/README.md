# TalkPipe codec / Celix service-boundary experiment

This lab turns the MessagePack hypothesis from `sandialabs/talkpipe#82` into a native Apache Celix experiment without changing TalkPipe's current JSON interface.

It measures three encodings of the same typed semantic plan:

1. compact JSON objects;
2. MessagePack maps with the same named fields;
3. MessagePack positional arrays with integer operation tags.

Two workloads are exercised:

- a deterministic 300-operation plan (`fetch`, `judge`, `emit`);
- a deterministic message carrying 64 KiB of binary evidence.

The encoded bytes cross a real Celix C++ service boundary. The provider decodes the bytes into the typed plan and re-encodes the same semantic object. The benchmark bundle then decodes the returned bytes and checks a canonical semantic SHA-256 at all three observation points:

```text
producer IR
   |
   v
codec bytes
   |
   v
Celix ITalkPipeWireService
   |
   +-- decode -> provider IR -> semantic SHA-256
   |
   +-- encode
   v
codec bytes
   |
   v
consumer IR -> semantic SHA-256
```

A successful result requires:

```text
producer semantic digest == provider semantic digest == consumer semantic digest
```

That check is deliberately codec-independent: the canonical semantic digest is computed from the same typed IR using the positional representation solely as a deterministic digest input.

## Run

From the repository root:

```bash
./experiments/celix_codec_bench/run.sh
```

The build is containerized. It installs the repository's pinned Apache Celix 2.4.0 release inside the image, builds the two bundles and Celix container, and emits one `TALKPIPE_BENCH` JSON record per workload/codec pair.

The host requires Docker only; the experiment does not install host packages.

Measured results from the clean three-run native experiment are recorded in [`RESULTS.md`](RESULTS.md).

## What this proves

If the semantic digests remain equal, TalkPipe semantics can cross a Celix service boundary independently of the selected codec.

If MessagePack is smaller/faster for a workload, that is evidence for an optional internal codec. It is not a reason to remove the existing JSON interface.

The positional representation is a separate optimization experiment. It should not become a stable TalkPipe wire contract without its own review.
