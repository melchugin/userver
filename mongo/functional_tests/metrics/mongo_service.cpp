#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/formats/bson/inline.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/server/handlers/tests_control.hpp>
#include <userver/storages/mongo/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include <userver/utest/using_namespace_userver.hpp>

namespace metrics {

class KeyValue final : public server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-key-value";

  KeyValue(const components::ComponentConfig& config,
           const components::ComponentContext& context)
      : HttpHandlerBase(config, context),
        pool_(context.FindComponent<components::Mongo>("key-value-database")
                  .GetPool()) {}

  std::string HandleRequestThrow(
      const server::http::HttpRequest& request,
      server::request::RequestContext&) const override {
    const auto& key = request.GetArg("key");
    const auto& value = request.GetArg("value");

    using formats::bson::MakeDoc;
    auto coll = pool_->GetCollection("test");
    coll.InsertOne(MakeDoc("key", key, "value", value));

    auto doc = coll.FindOne(MakeDoc("key", MakeDoc("$eq", key)));
    if (!doc) {
      throw server::handlers::ResourceNotFound{};
    }

    return (*doc)["value"].As<std::string>();
  }

 private:
  storages::mongo::PoolPtr pool_;
};

}  // namespace metrics

int main(int argc, char* argv[]) {
  const auto component_list =
      components::MinimalServerComponentList()
          .Append<clients::dns::Component>()
          .Append<components::HttpClient>()
          .Append<components::TestsuiteSupport>()
          .Append<server::handlers::ServerMonitor>()
          .Append<server::handlers::TestsControl>()
          .Append<components::Mongo>("key-value-database")
          .Append<metrics::KeyValue>();
  return utils::DaemonMain(argc, argv, component_list);
}
