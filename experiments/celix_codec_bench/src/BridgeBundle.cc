#include "TalkPipeCodecBench.h"

#include <memory>
#include <vector>

#include <celix/BundleActivator.h>

namespace {

class TalkPipeWireService final : public talkpipe::bench::ITalkPipeWireService {
public:
    talkpipe::bench::WireResult roundTrip(
            talkpipe::bench::Codec codec,
            const std::vector<std::uint8_t>& payload) override {
        const auto semantic = talkpipe::bench::decode(payload, codec);
        return {talkpipe::bench::encode(semantic, codec),
                talkpipe::bench::semanticSha256(semantic)};
    }
};

class BridgeBundleActivator final {
public:
    explicit BridgeBundleActivator(const std::shared_ptr<celix::BundleContext>& ctx) {
        service_ = std::make_shared<TalkPipeWireService>();
        registration_ = ctx->registerService<talkpipe::bench::ITalkPipeWireService>(service_).build();
    }

private:
    std::shared_ptr<TalkPipeWireService> service_{};
    std::shared_ptr<celix::ServiceRegistration> registration_{};
};

} // namespace

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BridgeBundleActivator)
