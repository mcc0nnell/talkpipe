#include "TalkPipeCodecBench.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <celix/BundleActivator.h>
#include <celix/Utils.h>
#include <celix_bundle_context.h>
#include <celix_framework.h>
#include <celix_constants.h>
#include <nlohmann/json.hpp>

namespace {

using Clock = std::chrono::steady_clock;
using talkpipe::bench::Codec;
using talkpipe::bench::ITalkPipeWireService;
using talkpipe::bench::Plan;

volatile std::size_t BENCH_SINK = 0;

template<typename Callable>
double operationsPerSecond(std::size_t iterations, Callable&& callable) {
    const auto started = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) callable();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
    return static_cast<double>(iterations) / elapsed;
}

void emitResult(const std::string& workload,
                const Plan& plan,
                Codec codec,
                ITalkPipeWireService& service,
                std::size_t iterations) {
    const auto payload = talkpipe::bench::encode(plan, codec);
    const auto producerDigest = talkpipe::bench::semanticSha256(plan);

    const auto encodeOps = operationsPerSecond(iterations, [&] {
        const auto encoded = talkpipe::bench::encode(plan, codec);
        BENCH_SINK += encoded.size();
    });

    const auto decodeOps = operationsPerSecond(iterations, [&] {
        const auto decoded = talkpipe::bench::decode(payload, codec);
        BENCH_SINK += decoded.operations.size();
    });

    const auto celixRoundTripOps = operationsPerSecond(iterations, [&] {
        const auto result = service.roundTrip(codec, payload);
        BENCH_SINK += result.payload.size();
    });

    const auto crossed = service.roundTrip(codec, payload);
    const auto consumer = talkpipe::bench::decode(crossed.payload, codec);
    const auto consumerDigest = talkpipe::bench::semanticSha256(consumer);
    const bool semanticEqual = plan == consumer &&
                               producerDigest == crossed.semanticSha256 &&
                               crossed.semanticSha256 == consumerDigest;

    const nlohmann::json result{
        {"workload", workload},
        {"codec", talkpipe::bench::codecName(codec)},
        {"bytes", payload.size()},
        {"encode_ops_s", encodeOps},
        {"decode_ops_s", decodeOps},
        {"celix_roundtrip_ops_s", celixRoundTripOps},
        {"iterations", iterations},
        {"semantic_equal", semanticEqual},
        {"producer_semantic_sha256", producerDigest},
        {"provider_semantic_sha256", crossed.semanticSha256},
        {"consumer_semantic_sha256", consumerDigest}
    };
    std::cout << "TALKPIPE_BENCH " << result.dump() << std::endl;
    if (!semanticEqual) throw std::runtime_error{"semantic mismatch across Celix service boundary"};
}

void runBenchmarks(ITalkPipeWireService& service) {
    const std::vector<Codec> codecs{
        Codec::Json,
        Codec::MessagePackMap,
        Codec::MessagePackPositional,
    };

    const auto plan = talkpipe::bench::makePlanWorkload();
    for (const auto codec : codecs) emitResult("plan-300", plan, codec, service, 2'000);

    const auto binary = talkpipe::bench::makeBinaryWorkload();
    for (const auto codec : codecs) emitResult("binary-64k", binary, codec, service, 1'000);
}

class BenchBundleActivator final {
public:
    explicit BenchBundleActivator(const std::shared_ptr<celix::BundleContext>& ctx)
        : worker_{[ctx] {
            const auto serviceName = celix::typeName<ITalkPipeWireService>();
            celix_service_use_options_t opts{};
            opts.filter.serviceName = serviceName.c_str();
            opts.filter.ignoreServiceLanguage = true;
            opts.waitTimeoutInSeconds = 5.0;
            opts.flags = CELIX_SERVICE_USE_DIRECT;
            opts.use = [](void*, void* rawService) {
                auto& service = *static_cast<ITalkPipeWireService*>(rawService);
                try {
                    runBenchmarks(service);
                    std::cout << "TALKPIPE_BENCH_DONE sink=" << BENCH_SINK << std::endl;
                } catch (const std::exception& ex) {
                    std::cerr << "TALKPIPE_BENCH_ERROR " << ex.what() << std::endl;
                }
            };
            const bool called = celix_bundleContext_useServiceWithOptions(
                ctx->getCBundleContext(), &opts);
            if (!called) {
                std::cerr << "TALKPIPE_BENCH_ERROR bridge service unavailable" << std::endl;
            }

            auto framework = ctx->getFramework();
            celix_framework_stopBundleAsync(framework->getCFramework(), CELIX_FRAMEWORK_BUNDLE_ID);
        }} {}

    ~BenchBundleActivator() {
        if (worker_.joinable()) worker_.join();
    }

private:
    std::thread worker_;
};

} // namespace

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BenchBundleActivator)
