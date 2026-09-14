# Native Celix codec experiment results

Date: 2026-09-14

This note records a bounded native C++ experiment motivated by `sandialabs/talkpipe#82`. It is not a TalkPipe production benchmark and does not change TalkPipe's existing JSON interface.

## Result

Three clean runs produced 18/18 successful workload/codec records, zero semantic mismatches, zero Celix warnings, and three zero exit codes.

For the deterministic 300-operation structured plan:

| Codec | Encoded bytes | Median Celix-held round trips/s | Observed range | Relative to JSON |
| --- | ---: | ---: | ---: | ---: |
| JSON | 14,711 | 1,101 | 1,060–1,152 | 1.0x |
| MessagePack map | 8,911 | 11,701 | 10,848–12,292 | 10.6x |
| MessagePack positional | 3,005 | 14,529 | 14,072–23,266 | 13.2x |

The positional representation was 79.6% smaller than the JSON representation in this workload.

For the deterministic message carrying 64 KiB of binary evidence:

| Codec | Encoded bytes | Median Celix-held round trips/s | Observed range | Relative to JSON |
| --- | ---: | ---: | ---: | ---: |
| JSON + base64 | 87,469 | 688 | 554–726 | 1.0x |
| MessagePack map | 65,590 | 7,166 | 6,824–7,188 | 10.4x |
| MessagePack positional | 65,550 | 7,290 | 6,893–7,361 | 10.6x |

The MessagePack representations were about 25.0% smaller because binary evidence is encoded natively instead of through base64.

## Semantic invariant

Every record checked the same typed semantic object at three observation points:

```text
producer semantic SHA-256
== provider semantic SHA-256
== consumer semantic SHA-256
```

All 18 records passed that invariant.

The digest is codec-independent at the semantic layer. It is calculated from the typed plan using a deterministic canonical representation only as digest input; it is not a claim that the positional wire format is already a stable protocol.

## What the round-trip number measures

The benchmark bundle obtains the provider through the Apache Celix service registry using `CELIX_SERVICE_USE_DIRECT`. Celix pins the service lifetime while the benchmark executes on a worker thread. Each measured round trip then calls the registered C++ service, where the provider:

1. receives encoded bytes;
2. decodes them into the typed plan;
3. computes the semantic digest;
4. re-encodes the same semantic plan;
5. returns the bytes to the consumer.

The consumer decodes the returned bytes and verifies semantic equality.

The benchmark does **not** perform a Celix service lookup for every message. It also does **not** cross a process, network, RSA, shared-memory, or socket boundary. The measured ratio is therefore evidence about native codec + typed-model execution through a Celix-managed service instance, not a universal distributed-transport speedup.

## Environment

- host architecture: x86_64
- host CPU: 2 vCPU, Intel Core Processor (Skylake, IBRS, no TSX)
- Docker Engine: 29.8.0
- container: Ubuntu 24.04
- compiler: GCC/G++ 13.3
- C++: C++17
- Apache Celix: 2.4.0, installed by the repository's SHA-512-pinned installer
- msgpack-cxx: Ubuntu `libmsgpack-cxx-dev` 6.1.0
- nlohmann JSON: 3.11.3
- build type: Release

Serializer/library choices are part of the result. This benchmark does not claim that every JSON implementation will have the same performance characteristics.

Throughput is sensitive to host scheduling. A separate clean three-run series on the same machine produced a higher positional-plan median, so the table above intentionally records the fresh public-branch reproduction rather than the most favorable observed series. Encoded sizes and semantic results were invariant.

## Reproduce

From the repository root:

```bash
RUNS=3 ./experiments/celix_codec_bench/run.sh | tee talkpipe-celix-codec-bench.log
```

A clean three-run result should contain:

```text
18 TALKPIPE_BENCH records
3 TALKPIPE_BENCH_DONE records
0 semantic_equal:false records
0 Celix warnings
3 zero exits
```

## Interpretation

The strongest result is not "MessagePack is always faster than JSON."

The experiment supports a narrower architecture hypothesis:

```text
TalkPipe typed semantic model
        |             |
        v             v
      JSON       MessagePack
 compatibility   internal/native path
```

On this native C++ workload, the optional MessagePack path preserved semantics while materially reducing encoded size and increasing encode/decode + registered-service round-trip throughput.

The positional representation produced the strongest structured-plan result, but it is a second optimization decision and should remain separate from the initial decision to support an optional MessagePack codec.
