#include <userver/components/statistics_storage.hpp>

USERVER_NAMESPACE_BEGIN

namespace components {

StatisticsStorage::StatisticsStorage(
    const ComponentConfig& config, const components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      metrics_storage_(std::make_shared<utils::statistics::MetricsStorage>()) {
  statistics_holder_ = storage_.RegisterExtender(
      "", [this](const auto& request) { return ExtendStatistics(request); });
}

StatisticsStorage::~StatisticsStorage() { statistics_holder_.Unregister(); }

void StatisticsStorage::OnAllComponentsLoaded() {
  storage_.StopRegisteringExtenders();
}

formats::json::ValueBuilder StatisticsStorage::ExtendStatistics(
    const utils::statistics::StatisticsRequest& request) {
  return metrics_storage_->DumpMetrics(request.prefix);
}

std::string StatisticsStorage::GetStaticConfigSchema() {
  return R"(
type: object
description: statistics-storage config
additionalProperties: false
properties: {}
)";
}

}  // namespace components

USERVER_NAMESPACE_END
