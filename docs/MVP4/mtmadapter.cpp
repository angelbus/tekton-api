#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/reader.h>   // Required for arrow::ipc::RecordBatchStreamReader
#include <arrow/util/base64.h>   // Required for arrow::util::base64_decode



try {
    // 0. Extract fields from JSON input
    std::string inputPath;
    std::string resultPath;
    std::string errorPath;
    std::string calibratedMarketBase;
    std::string sessionId;
    std::optional<int> rowGroup;
    std::optional<int> dealIndex;
    std::optional<int> dealCount;
    std::string progressCounterKey;
    std::string progressBookId;
    std::string taskId;
    bool reconciliation = false;
    int retries = S3_DEFAULT_RETRIES;
    std::string retries_source = "default";
    std::string rowGroupData;

    {
        nlohmann::json j = nlohmann::json::parse(serializedRequest);
        inputPath            = j.value("input_file", "");
        resultPath           = j.value("results_path", "");
        errorPath            = j.value("error_path", "");
        calibratedMarketBase = j.value("calibrated_market", "");
        sessionId            = j.value("session_id", "");
        taskId               = j.value("task_id", "");
        progressCounterKey   = j.value("progress_counter_key", "");
        progressBookId       = j.value("progress_book_id", "");
        reconciliation       = j.value("reconciliation", false);
        rowGroupData         = j.value("row_group_data", "");

        if (j.contains("retries") && j["retries"].is_number_integer()) {
            retries = std::clamp(j["retries"].get<int>(), 0, 10);
            retries_source = "payload";
        }
        if (j.contains("row_group") && j["row_group"].is_number_integer()) {
            const auto value = j["row_group"].get<int>();
            if (value >= 0) rowGroup = value;
        }
        if (j.contains("deal_index") && j["deal_index"].is_number_integer()) {
            dealIndex = j["deal_index"].get<int>();
        }
        if (j.contains("deal_count") && j["deal_count"].is_number_integer()) {
            dealCount = j["deal_count"].get<int>();
        }
    }

    // Strip leading "s3://" if present
    auto stripS3Prefix = [](std::string path) -> std::string {
        if (path.rfind("s3://", 0) == 0) path = path.substr(5);
        return path;
    };

    inputPath            = stripS3Prefix(inputPath);
    resultPath           = stripS3Prefix(resultPath);
    errorPath            = stripS3Prefix(errorPath);
    calibratedMarketBase = stripS3Prefix(calibratedMarketBase);

    const int max_attempts = retries + 1;
    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: retries=" + std::to_string(retries) +
        " (total attempts=" + std::to_string(max_attempts) +
        ", source=" + retries_source + ")");

    if (inputPath.empty() || resultPath.empty() || errorPath.empty()) {
        const std::string msg = "FMTMAdapter: Missing required JSON fields (input_file, results_path, error_path)";
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);
        return {QL_ADAPTER_ERR_MISSING_JSON_FIELDS, make_result_json("error", QL_ADAPTER_ERR_MISSING_JSON_FIELDS, msg)};
    }

    if (calibratedMarketBase.back() == '/') calibratedMarketBase.pop_back();
    const std::string calibratedMarket = calibratedMarketBase + "/engine.parquet";
    auto [resultBucket, resultKey] = splitBucketKey(resultPath);
    auto [errorBucket, errorKey]   = splitBucketKey(errorPath);

    // Ensure folder keys end with '/'
    if (!resultKey.empty() && resultKey.back() != '/') resultKey += '/';
    errorKey = normalizeDirectoryKey(errorKey);

    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: inputPath='" + inputPath +
        "' resultPath='" + resultPath +
        "' errorPath='" + errorPath +
        "' calibratedMarket='" + calibratedMarket + "'");

    // Connect to Redis for netting set tracking
    data_references_tmpl<data_references_mto> redis;
    bool redisConnected = redis.connect();
    if (!redisConnected) {
        mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
            "FMTMAdapter: Redis not available - continuing without netting set tracking");
    }

    // 1. Load the simulation library
    CalibrationUtility util;
    const std::string libPath = util.GetLibraryPath();
    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: Using simulation library: " + libPath);
    LoadLibrary(libPath);
    LoadSymbols();

    auto [inputBucket, inputKey] = splitBucketKey(inputPath);
    std::string s3FetchErr;
    std::vector<Deal> deals;
    // Values produced during the fetch phase and consumed by the deal
    // processing loop below (after the fetch connector is destroyed).
    std::size_t deal_count = 0;
    std::string engine_data;
    std::string static_data;

    if (rowGroup.has_value()) {
        std::string row_group_log =
            "FMTMAdapter: Reading row group " + std::to_string(*rowGroup);
        if (dealIndex.has_value()) {
            row_group_log += ", deal offset " + std::to_string(*dealIndex) +
                             ", count " + std::to_string(dealCount.value_or(1));
        }
        row_group_log += " from parquet path='" + inputPath + "'";
        mtoapi::MtoLogger::log(mtoapi::LogLevel::info, row_group_log);

        if (rowGroupData.empty()) {
            const std::string msg = "FMTMAdapter: Embedded row_group_data is empty";
            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);
            return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
        }

        // 1. Base64 decode raw_bytes embedded in JSON payload
        std::string raw_bytes = arrow::util::base64_decode(rowGroupData);

        // 2. Deserialize Arrow IPC stream directly from memory buffer
        auto buffer = arrow::Buffer::Wrap(raw_bytes.data(), raw_bytes.size());
        auto buffer_reader = std::make_shared<arrow::io::BufferReader>(buffer);
        auto stream_reader_result = arrow::ipc::RecordBatchStreamReader::Open(buffer_reader);

        if (!stream_reader_result.ok()) {
            const std::string msg = "FMTMAdapter: Failed to open Arrow RecordBatchStreamReader: " +
                                    stream_reader_result.status().ToString();
            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);
            return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
        }

        auto stream_reader = *stream_reader_result;
        std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
        std::shared_ptr<arrow::RecordBatch> batch;

        while (true) {
            auto read_batch_status = stream_reader->ReadNext(&batch);
            if (!read_batch_status.ok()) {
                const std::string msg = "FMTMAdapter: Failed to read batch from Arrow IPC stream: " +
                                        read_batch_status.ToString();
                mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
                pushErrorLog(msg);
                return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
            }
            if (!batch) break;
            batches.push_back(batch);
        }

        auto table_result = arrow::Table::FromRecordBatches(stream_reader->schema(), batches);
        if (!table_result.ok()) {
            const std::string msg = "FMTMAdapter: Failed to build Arrow table from batches: " +
                                    table_result.status().ToString();
            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);
            return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
        }

        std::shared_ptr<arrow::Table> row_group_table = *table_result;

        // 3. Decompose Arrow Table into deals
        int64_t name_offset = 0;
        deals = deal_decompositor_.DecomposeTable(std::move(row_group_table), name_offset);
    }

    if (dealIndex.has_value()) {
        if (*dealIndex < 0 ||
            static_cast<std::size_t>(*dealIndex) >= deals.size() ||
            (dealCount.has_value() && *dealCount <= 0)) {
            const std::string msg =
                "FMTMAdapter: Invalid deal slice for row group " +
                std::to_string(*rowGroup) + " (offset=" +
                std::to_string(*dealIndex) + ", count=" +
                std::to_string(dealCount.value_or(1)) + ")";
            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);
            return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
        }
        const std::size_t offset = static_cast<std::size_t>(*dealIndex);
        const std::size_t available = deals.size() - offset;
        const std::size_t selected_count = std::min(
            available, static_cast<std::size_t>(dealCount.value_or(1)));
        std::vector<Deal> selected_deals;
        selected_deals.reserve(selected_count);
        for (std::size_t i = 0; i < selected_count; ++i) {
            selected_deals.push_back(std::move(deals[offset + i]));
        }
        deals = std::move(selected_deals);
    }





-------






// 4. Fetch engine parquet using Redis cache (Read-Through pattern)
std::string cache_key = "cache:calibrated_market:" + calibratedMarket;
std::string engine_response_bytes;
bool fetched_from_cache = false;

// Attempt to fetch from Redis if connected
if (redisConnected) {
    try {
        std::string cached_val;
        if (redis.get(cache_key, cached_val) && !cached_val.empty()) {
            engine_response_bytes = std::move(cached_val);
            fetched_from_cache = true;
            mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                "FMTMAdapter: Engine parquet loaded from Redis cache (key='" + cache_key + "')");
        }
    } catch (...) {
        mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
            "FMTMAdapter: Redis lookup failed for engine parquet cache key='" + cache_key + "'");
    }
}

// Fallback to S3 if Redis missed or unavailable
if (!fetched_from_cache) {
    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: Fetching engine parquet from S3 path='" + calibratedMarket + "'");

    auto [engineBucket, engineKey] = splitBucketKey(calibratedMarket);

    const bool engine_fetched = RetryTransientOperation(
        "engine parquet fetch",
        max_attempts,
        [&](std::string& error) {
            const S3Status status = s3.GetObjectToString(
                engineBucket, engineKey, engine_response_bytes, &error);
            s3FetchErr = error;
            return ClassifyS3Status(status);
        },
        s3FetchErr);

    if (!engine_fetched) {
        const std::string msg = "FMTMAdapter: Failed to fetch engine parquet from S3 (path='" + calibratedMarket + "'): " + s3FetchErr;
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);
        return {QL_ADAPTER_ERR_S3_FETCH_FAILED, make_result_json("error", QL_ADAPTER_ERR_S3_FETCH_FAILED, msg)};
    }

    if (engine_response_bytes.empty()) {
        const std::string msg = "FMTMAdapter: Engine parquet from S3 is empty (path='" + calibratedMarket + "')";
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);
        return {QL_ADAPTER_ERR_S3_FETCH_EMPTY, make_result_json("error", QL_ADAPTER_ERR_S3_FETCH_EMPTY, msg)};
    }

    // Write-back to Redis for subsequent workers (TTL: 24 hours = 86400s)
    if (redisConnected) {
        try {
            redis.setex(cache_key, 86400, engine_response_bytes);
            mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                "FMTMAdapter: Cached engine parquet in Redis (key='" + cache_key + "')");
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Failed to set engine parquet in Redis cache");
        }
    }
}

// Deserialize Engine Data
EngineResponse engine_response = EngineResponse::DeserializeResponse(engine_response_bytes);
engine_data = engine_response.GetEngine();
static_data = engine_response.GetStaticData();

mtoapi::MtoLogger::log(mtoapi::LogLevel::info, "FMTMAdapter: Engine response loaded");










-------
  
