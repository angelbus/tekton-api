#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/reader.h>   // Required for arrow::ipc::RecordBatchStreamReader
#include <arrow/util/base64.h>   // Required for arrow::util::base64_decode


#include <mutex>          // Required for std::mutex and std::lock_guard
#include <unordered_map>  // Required for std::unordered_map

namespace {
    struct CalibratedMarketData {
        EngineData engine_data;
        StaticData static_data;
    };

    // Thread-safe process-level POD cache
    static std::mutex g_market_cache_mutex;
    static std::unordered_map<std::string, CalibratedMarketData> g_market_cache;
}

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






// 4. Fetch engine parquet using Redis cache (Read-Through pattern via Hash)
const std::string hash_name = "cache:calibrated_market";
const std::string field_key = calibratedMarket;
std::string engine_response_bytes;
bool fetched_from_cache = false;

// Attempt to fetch from Redis Hash if connected
if (redisConnected) {
    try {
        // hget(hash_name, field_key, disable_keyerror = true)
        std::string cached_val = redis.hget(hash_name, field_key, true);
        if (!cached_val.empty()) {
            engine_response_bytes = std::move(cached_val);
            fetched_from_cache = true;
            mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                "FMTMAdapter: Engine parquet loaded from Redis cache (hash='" + hash_name +
                "', field='" + field_key + "')");
        }
    } catch (...) {
        mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
            "FMTMAdapter: Redis hget failed for engine parquet cache field='" + field_key + "'");
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

    // Write-back to Redis Hash with TTL (10 hours = 36000s)
    if (redisConnected) {
        try {
            // hset(hash_name, field_key, value, ttl)
            const int rc = redis.hset(hash_name, field_key, engine_response_bytes, 36000);
            if (rc >= 0) {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                    "FMTMAdapter: Cached engine parquet in Redis hash='" + hash_name +
                    "' field='" + field_key + "' with 10h TTL");
            } else {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                    "FMTMAdapter: Redis hset returned error code " + std::to_string(rc));
            }
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Failed to hset engine parquet in Redis cache");
        }
    }
}

// Deserialize Engine Data
EngineResponse engine_response = EngineResponse::DeserializeResponse(engine_response_bytes);
engine_data = engine_response.GetEngine();
static_data = engine_response.GetStaticData();

mtoapi::MtoLogger::log(mtoapi::LogLevel::info, "FMTMAdapter: Engine response loaded");


-----


auto [inputBucket, inputKey] = splitBucketKey(inputPath);

std::string s3FetchErr;
std::vector<Deal> deals;

// Values produced during the fetch phase and consumed by the deal
// processing loop below (after the fetch connector is destroyed).
std::size_t deal_count = 0;
std::string engine_data;
std::string static_data;

if (rowGroup.has_value()) {
    const std::string row_group_log =
        "FMTMAdapter: Reading embedded slice for row group " +
        std::to_string(*rowGroup) +
        ", count " +
        std::to_string(dealCount.value_or(0)) +
        " from parquet path='" + inputPath + "'";

    mtoapi::MtoLogger::log(mtoapi::LogLevel::info, row_group_log);

    if (sliceData.empty()) {
        const std::string msg =
            "FMTMAdapter: Embedded slice_data is empty";

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    if (!dealCount.has_value() || *dealCount <= 0) {
        const std::string msg =
            "FMTMAdapter: Invalid or missing deal_count";

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    // 1. Base64 decode raw_bytes embedded in JSON payload
    std::string raw_bytes =
        arrow::util::base64_decode(sliceData);

    // 2. Deserialize Arrow IPC stream directly from memory buffer
    auto buffer =
        arrow::Buffer::Wrap(raw_bytes.data(), raw_bytes.size());

    auto buffer_reader =
        std::make_shared<arrow::io::BufferReader>(buffer);

    auto stream_reader_result =
        arrow::ipc::RecordBatchStreamReader::Open(buffer_reader);

    if (!stream_reader_result.ok()) {
        const std::string msg =
            "FMTMAdapter: Failed to open Arrow RecordBatchStreamReader: " +
            stream_reader_result.status().ToString();

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    auto stream_reader = *stream_reader_result;

    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    std::shared_ptr<arrow::RecordBatch> batch;

    while (true) {
        auto read_batch_status =
            stream_reader->ReadNext(&batch);

        if (!read_batch_status.ok()) {
            const std::string msg =
                "FMTMAdapter: Failed to read batch from Arrow IPC stream: " +
                read_batch_status.ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        if (!batch)
            break;

        batches.push_back(batch);
    }

    auto table_result =
        arrow::Table::FromRecordBatches(
            stream_reader->schema(),
            batches);

    if (!table_result.ok()) {
        const std::string msg =
            "FMTMAdapter: Failed to build Arrow table from batches: " +
            table_result.status().ToString();

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    std::shared_ptr<arrow::Table> slice_table = *table_result;

    // 3. Decompose Arrow Table into deals
    int64_t name_offset = 0;

    deals = deal_decompositor_.DecomposeTable(
        std::move(slice_table),
        name_offset);

    if (deals.size() != static_cast<std::size_t>(*dealCount)) {
        const std::string msg =
            "FMTMAdapter: Embedded slice deal count mismatch for row group " +
            std::to_string(*rowGroup) +
            ": expected=" + std::to_string(*dealCount) +
            ", actual=" + std::to_string(deals.size());

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }
}


-------

private:
    bool IsDealProcessed(const std::string& hash_name, const std::string& deal_field_key) {
        if (!redisConnected) {
            return false;
        }

        try {
            // disable_keyerror = true returns empty string if field doesn't exist
            const std::string deal_status = redis.hget(hash_name, deal_field_key, true);
            if (deal_status == "true") {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                    "FMTMAdapter: Deal " + deal_field_key + " already processed. Skipping.");
                return true;
            }
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Redis hget failed for deal " + deal_field_key + 
                ". Proceeding with normal execution.");
        }

        return false;
    }

    if (IsDealProcessed(hashName, deal_field_key)) {
        continue;
    }
    
---------------

for (std::size_t i = 0; i < deal_count; ++i) {
    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: Processing deal " + std::to_string(i + 1) +
        "/" + std::to_string(deal_count));

    const DealOutputPaths output_paths = output_paths_for(deals[i], i);

    // 1. Check idempotency: Has this specific deal already been processed?
    const std::string hashName = NettingRedisKey(output_paths.netting_set);
    const std::string deal_field_key = "deal_processed:" + deals[i].GetId(); // Or deals[i].GetName()

    if (redisConnected) {
        try {
            // disable_keyerror = true returns empty string if field doesn't exist
            std::string deal_status = redis.hget(hashName, deal_field_key, true);
            if (deal_status == "true") {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                    "FMTMAdapter: Deal " + std::to_string(i + 1) +
                    " (" + deals[i].GetId() + ") already processed. Skipping.");
                continue; // Skip processing and jump to the next deal
            }
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Redis hget failed for deal " + deals[i].GetId() +
                ". Proceeding with normal execution.");
        }
    }

    if (reconciliation) {
        int64_t existing_size = 0;
        std::string s3err;
        // ... reconciliation logic ...
    }

    // ============================================================
    // Perform deal processing / output writing here
    // ============================================================

    // 2. Mark deal as successfully processed in Redis
    if (redisConnected) {
        try {
            // hset(hashName, field, value, ttl)
            const int set_rc = redis.hset(hashName, deal_field_key, "true", 36000); // 10h TTL
            if (set_rc < 0) {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                    "FMTMAdapter: Failed to set idempotency flag in Redis for deal " + deals[i].GetId());
            }
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Exception while marking deal as processed in Redis: " + deals[i].GetId());
        }
    }
}


-------

// 4. Fetch engine parquet using multi-tier cache (POD RAM -> Redis -> S3)
auto [engineBucket, engineKey] = splitBucketKey(calibratedMarket);
const std::string hash_name = MarketRedisKey(engineKey);
const std::string field_engine = "engine_data";
const std::string field_static = "static_data";

bool fetched_from_pod_cache = false;
bool fetched_from_redis = false;

// Tier 1: Check Local POD Memory Cache
{
    std::lock_guard<std::mutex> lock(g_market_cache_mutex);
    auto it = g_market_cache.find(calibratedMarket);
    if (it != g_market_cache.end()) {
        engine_data = it->second.engine_data;
        static_data = it->second.static_data;
        fetched_from_pod_cache = true;
        mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
            "FMTMAdapter: Engine response loaded directly from POD local memory cache (key='" + calibratedMarket + "')");
    }
}

if (!fetched_from_pod_cache) {
    // Tier 2: Attempt to fetch deserialized JSONs from Redis Hash if connected
    if (redisConnected) {
        try {
            // hget(hash_name, field_key, disable_keyerror = true)
            std::string cached_engine = redis.hget(hash_name, field_engine, true);
            std::string cached_static = redis.hget(hash_name, field_static, true);

            if (!cached_engine.empty() && !cached_static.empty()) {
                engine_data = std::move(cached_engine);
                static_data = std::move(cached_static);
                fetched_from_redis = true;
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                    "FMTMAdapter: Engine & Static data JSONs loaded from Redis cache (hash='" + hash_name + "')");
            }
        } catch (...) {
            mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                "FMTMAdapter: Redis hget failed for engine/static data in hash='" + hash_name + "'");
        }
    }

    // Tier 3: Fallback to S3 if Redis missed or unavailable
    if (!fetched_from_redis) {
        mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
            "FMTMAdapter: Fetching engine parquet from S3 path='" + calibratedMarket + "'");

        std::string engine_response_bytes;
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

        // Deserialize Engine Parquet into JSON structures
        EngineResponse engine_response = EngineResponse::DeserializeResponse(engine_response_bytes);
        engine_data = engine_response.GetEngine();
        static_data = engine_response.GetStaticData();

        // Write-back deserialized JSONs to Redis Hash with TTL
        if (redisConnected) {
            try {
                const int rc1 = redis.hset(hash_name, field_engine, engine_data, CACHE_DEFAULT_TTL);
                const int rc2 = redis.hset(hash_name, field_static, static_data, CACHE_DEFAULT_TTL);

                if (rc1 >= 0 && rc2 >= 0) {
                    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                        "FMTMAdapter: Cached deserialized engine & static data JSONs in Redis hash='" + hash_name + "' with 10h TTL");
                } else {
                    mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                        "FMTMAdapter: Redis hset returned error code (engine_rc=" + std::to_string(rc1) +
                        ", static_rc=" + std::to_string(rc2) + ")");
                }
            } catch (...) {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::warn,
                    "FMTMAdapter: Failed to hset deserialized market data in Redis cache");
            }
        }
    }

    // Populate Tier 1 POD Memory Cache for subsequent tasks on this POD
    {
        std::lock_guard<std::mutex> lock(g_market_cache_mutex);
        g_market_cache[calibratedMarket] = {engine_data, static_data};
    }

    mtoapi::MtoLogger::log(mtoapi::LogLevel::info, "FMTMAdapter: Engine response loaded");
}

  
