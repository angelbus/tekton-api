const std::string hashName = NettingRedisKey(output_paths.netting_set);
try {
    long long processed = redis.hincrby(hashName, "processed", 1);
    std::string expectedStr = redis.hget(hashName, "expected", true);
    long long expected = expectedStr.empty() ? 0 : std::stoll(expectedStr);

    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "FMTMAdapter: Redis NS '" + output_paths.netting_set + 
        "' processed=" + std::to_string(processed) + 
        "/" + std::to_string(expected));

    // 1. Check for invalid expected count
    if (expected <= 0) {
        std::string err = "FMTMAdapter: Invalid expected count (" + 
                          std::to_string(expected) + ") for NS '" + output_paths.netting_set + "'";
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, err);
        pushErrorLog(err);
    } 
    // 2. Exact completion: upload manifest ONCE atomically
    else if (processed == expected) {
        // Atomic Lock: HSETNX returns true ONLY for the first caller that sets "manifest_uploaded" to "true"
        const bool is_first_completion = redis.hsetnx(hashName, "manifest_uploaded", "true");

        if (is_first_completion) {
            redis.hset(hashName, "status", "COMPLETED", -1);

            auto now_ts = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now_ts);
            std::ostringstream tss;
            tss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");

            std::string csaFromRedis = redis.hget(hashName, "collateralagreement", true);

            nlohmann::json ns_manifest;
            ns_manifest["netting_set"]           = output_paths.netting_set;
            ns_manifest["collateral_agreement"] = csaFromRedis.empty() ? output_paths.csa : csaFromRedis;
            ns_manifest["expected_deals"]       = expected;
            ns_manifest["processed_deals"]      = expected; // Clean exact match
            ns_manifest["status"]               = "COMPLETED";
            ns_manifest["completed_at"]          = tss.str();
            ns_manifest["session_id"]            = sessionId;

            const std::string nsManifestKey = resultKey + output_paths.ns_folder + "/nettingset_manifest.json";
            std::string s3ManifestErr;
            const std::string serialized_manifest = ns_manifest.dump(4);
            
            const bool manifest_uploaded = RetryTransientOperation(
                "nettingset manifest upload",
                max_attempts,
                [&](std::string& error) {
                    const S3Status status = s3.PutString(
                        resultBucket, nsManifestKey, serialized_manifest, &error);
                    s3ManifestErr = error;
                    return ClassifyS3Status(status);
                },
                s3ManifestErr);

            if (!manifest_uploaded) {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::error,
                    "FMTMAdapter: Failed to upload nettingset_manifest.json for " + output_paths.netting_set + ": " + s3ManifestErr);
            } else {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
                    "FMTMAdapter: nettingset_manifest.json uploaded for NS '" + output_paths.netting_set + 
                    "' (" + std::to_string(processed) + "/" + std::to_string(expected) + " deals)");
            }
        }
    } 
    // 3. Evidence Gathering: Log over-processing duplicates
    else if (processed > expected) {
        std::string overprocess_err = "FMTMAdapter: OVER-PROCESSING DETECTED for NS '" + output_paths.netting_set + 
                                      "'! Current processed (" + std::to_string(processed) + 
                                      ") exceeds expected (" + std::to_string(expected) + ")";
        
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, overprocess_err);
        pushErrorLog(overprocess_err);
    }
} catch (const std::exception& e) {
    mtoapi::MtoLogger::log(mtoapi::LogLevel::error,
        "FMTMAdapter: Exception in Redis completion block: " + std::string(e.what()));
}
