#include "xena/s3_connector/S3Connector.h"
#include "xena/loader/library_adapter_factory.h"
#include "xena/loader/adapters/calibration_library_adapter.h"
#include "xena/loader/adapters/fmtm_adapter.h"
#include "xena/loader/adapters/xde_adapter.h"
#include "xena/loader/adapters/dag_adapter.h"
#include "xena/loader/adapters/aggregate_adapter.h"
#include "xena/loader/adapters/price_ns_adapter.h"
#include "xena/loader/adapters/crif_adapter.h"
#include "xena/loader/adapters/prep_adapter.h"
#include "xena/loader/adapters/deal_simulation_adapter.h"
#include "mto/mtoapi/MtoLogger.h"

#include <memory>

void InitAdapters() {
    // 1. Create a single instance owned by the loader module lifecycle
    static auto s3_connector = std::make_shared<S3Connector>();

    // 2. Inject s3_connector into adapters requiring S3 access
    RegisterAdapter("QUANT", [s3_connector]{  return std::make_unique<CalibrationLibraryAdapter>(s3_connector);  });
    RegisterAdapter("FMTM", [s3_connector]{  return std::make_unique<XDEAdapter>(s3_connector); );
    RegisterAdapter("FMTM_JOB", [s3_connector]{  return std::make_unique<FMTMAdapter>(s3_connector); });
    RegisterAdapter("DAGREQUEST", [s3_connector]{  return std::make_unique<DAGAdapter>(s3_connector); });
    RegisterAdapter("CUBES_AGGREGATION", [s3_connector]{  return std::make_unique<AggregateAdapter>(s3_connector); );
    RegisterAdapter("CUBE_PRICING", [s3_connector]{  return std::make_unique<PriceNSAdapter>(s3_connector); });
    RegisterAdapter("CRIF", [s3_connector]{  return std::make_unique<CRIFAdapter>(s3_connector); });
    RegisterAdapter("PREP", [s3_connector]{  return std::make_unique<PrepAdapter>(s3_connector);});
    RegisterAdapter("DEAL_SIMULATION", [s3_connector]{  return std::make_unique<DealSimulationAdapter>(s3_connector);});

    // ... RegisterLibTypeDetector logic ...
}


      ----

      class XDEAdapter : public BaseAdapter {
public:
    // Accept injected shared pointer or reference
    explicit XDEAdapter(std::shared_ptr<S3Connector> s3_connector);

private:
    std::shared_ptr<S3Connector> s3_connector_;
};

-----
  XDEAdapter::XDEAdapter(std::shared_ptr<S3Connector> s3_connector)
    : s3_connector_(std::move(s3_connector)) {}
